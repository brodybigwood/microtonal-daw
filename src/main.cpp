#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"
#include "NodeProcessor.h"
#include "styles.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <thread>
#include <iostream>
#include <cstdlib>
#include <string>
#include <atomic>

int main(int argc, char* argv[]) {

    if(!initFonts()) {
        std::cerr << "ttf init failed" << std::endl;
        return -1;
    }

    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
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

    if (!audioManager->start())
        std::cout << "[Audio] failed to start, running without audio" << std::endl;

    project->setup();

#ifdef __EMSCRIPTEN__
    static Project* g_project = project;
    emscripten_set_main_loop([]() {
        g_project->processor->setThreadActiveRoot(g_project->processor->guiManager);
        WindowHandler::instance()->tick();
    }, 0, 1);
#else
    while (windowHandler->tick()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
#endif

    project->save();
    audioManager->stop();
    delete project;
    SDL_Quit();

    return 0;
}
