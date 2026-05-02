#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"
#include "styles.h"

#include <thread>
#include <csignal>
#include <iostream>
#include <cstdlib>
#include <string>
#include <atomic>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include <sstream>

static std::atomic<bool> g_stopRequested{false};

/** Saved at startup so we can restore canonical+echo if we exit while stdinThread left the TTY raw. */
static termios g_stdinTermiosAtStartup{};
static bool g_stdinTermiosAtStartupValid = false;

static void restoreStdinTermiosAtExit() {
    if (g_stdinTermiosAtStartupValid && isatty(STDIN_FILENO))
        (void)tcsetattr(STDIN_FILENO, TCSADRAIN, &g_stdinTermiosAtStartup);
}

static std::string commandHint(const std::string& line) {
    std::stringstream ss(line);
    std::string op, arg;
    ss >> op;
    if (op == "action") {
        ss >> arg;
        if (arg.empty()) return "hint: action <name> ...";
        if (arg == "add_node") return "hint: <path> <nodeType> <x> <y>";
        if (arg == "remove_node") return "hint: <path> <nodeID>";
        if (arg == "make_node_connection") return "hint: <path> <srcNode> <srcCon> <dstNode> <dstCon>";
        if (arg == "sever_node_connection") return "hint: <path> <srcNode> <srcCon> <dstNode> <dstCon>";
        if (arg == "move_node") return "hint: <path> <nodeID> <toX> <toY>";
        if (arg == "add_arranger_track") return "hint: <path> <nodeID> <trackType>";
        if (arg == "create_note") return "hint: <path> <nodeID> <regionID> <sNum> <sDen> <lNum> <lDen> <pitch>";
    } else if (op == "node_types") {
        return "hint: lists node type IDs";
    } else if (op == "action_schema") {
        return "hint: action_schema <name>";
    }
    return "";
}

static bool readCommandLineRaw(std::string& outLine, std::vector<std::string>& history, int& historyIndex) {
    termios oldt{};
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) return false;
    termios raw = oldt;
    raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return false;

    size_t cursorPos = 0;
    outLine.clear();

    auto redraw = [&]() {
        std::string hint = commandHint(outLine);
        if (hint.size() > 72) hint = hint.substr(0, 72);
        const size_t promptLen = 5; // "cmd> "
        std::cout << "\r\33[2K" << "cmd> " << outLine;
        std::cout << "\n\33[2K";
        if (!hint.empty()) std::cout << hint;
        std::cout << "\33[1A\r";
        std::cout << "\33[" << (promptLen + cursorPos + 1) << "G" << std::flush;
    };

    redraw();
    while (true) {
        char ch = 0;
        ssize_t n = ::read(STDIN_FILENO, &ch, 1);
        if (n <= 0) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            return false;
        }
        if (ch == '\n' || ch == '\r') {
            std::cout << "\r\33[2Kcmd> " << outLine << "\n\33[2K\n\33[1A" << std::flush;
            break;
        }
        if (ch == 127 || ch == '\b') {
            if (cursorPos > 0) {
                outLine.erase(cursorPos - 1, 1);
                cursorPos--;
            }
            redraw();
            continue;
        }
        if (ch == 27) {
            char s1 = 0, s2 = 0;
            if (::read(STDIN_FILENO, &s1, 1) <= 0 || ::read(STDIN_FILENO, &s2, 1) <= 0) {
                tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
                return false;
            }
            if (s1 == '[' && s2 == 'A') {
                if (!history.empty()) {
                    if (historyIndex < 0) historyIndex = static_cast<int>(history.size()) - 1;
                    else if (historyIndex > 0) historyIndex--;
                    outLine = history[historyIndex];
                    cursorPos = outLine.size();
                }
            } else if (s1 == '[' && s2 == 'B') {
                if (!history.empty()) {
                    if (historyIndex >= 0 && historyIndex < static_cast<int>(history.size()) - 1) {
                        historyIndex++;
                        outLine = history[historyIndex];
                    } else {
                        historyIndex = -1;
                        outLine.clear();
                    }
                    cursorPos = outLine.size();
                }
            } else if (s1 == '[' && s2 == 'C') {
                if (cursorPos < outLine.size()) cursorPos++;
            } else if (s1 == '[' && s2 == 'D') {
                if (cursorPos > 0) cursorPos--;
            }
            redraw();
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            outLine.insert(outLine.begin() + static_cast<long>(cursorPos), ch);
            cursorPos++;
            redraw();
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return true;
}


int main(int argc, char* argv[]) {

    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_stdinTermiosAtStartup) == 0) {
        g_stdinTermiosAtStartupValid = true;
        std::atexit(restoreStdinTermiosAtExit);
    }

    std::signal(SIGINT, [](int) {
        restoreStdinTermiosAtExit();
        g_stopRequested.store(true);
    });
    
    if(!initFonts()) {
        std::cerr << "ttf init failed failed" << std::endl;
        return -1;
    }

    if(!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "sdl init failed failed" << std::endl;
        return -1;
    }

    createCursors();

    Project* project = new Project;
    AudioManager* audioManager = AudioManager::instance();
    audioManager->setProject(project);

    
    if (argc > 1) {
        project->load(argv[1]);
    }


    WindowHandler* windowHandler = WindowHandler::instance();
    windowHandler->project = project;

    std::thread stdinThread([windowHandler]() {
        std::string line;
        std::vector<std::string> history;
        history.reserve(1000);
        int historyIndex = -1;
        while (true) {
            if (!readCommandLineRaw(line, history, historyIndex)) break;
            if (line.empty()) continue;
            history.push_back(line);
            if (history.size() > 1000) history.erase(history.begin());
            historyIndex = -1;
            windowHandler->enqueueCommand(line);
        }
    });
    stdinThread.detach();

    if (!audioManager->start()) {
        std::cout << "audiomanager failed" << std::endl;
        return -1;
    }

    project->setup();


    while (!g_stopRequested.load() && windowHandler->tick()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 1000ms / 200Hz = 5ms
    }

    project->save();
    audioManager->stop();

    // SDL_DestroyWindow must run before SDL_Quit (Quit tears down video first).
    delete audioManager;
    delete project;
    SDL_Quit();

    return 0;
}
