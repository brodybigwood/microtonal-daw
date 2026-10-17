#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"
#include "NodeProcessor.h"
#include "ControlServer.h"
#include "Settings.h"
#include "styles.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <thread>
#include <iostream>
#include <cstdlib>
#include <string>
#include <atomic>
#include <csignal>

static std::atomic<bool> quit{false};

int main(int argc, char* argv[]) {
    std::signal(SIGINT, [](int){ quit.store(true); });
    std::signal(SIGTERM, [](int){ quit.store(true); });

    if(!initFonts()) {
        std::cerr << "ttf init failed" << std::endl;
        return -1;
    }

    if(!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::cerr << "SDL audio init failed (non-fatal): " << SDL_GetError() << std::endl;
    }

    createCursors();
    loadColorsFromSettings();

    Project* project = new Project;
    AudioManager* audioManager = AudioManager::instance();
    audioManager->setProject(project);

    if (argc > 1) {
        project->load(argv[1]);
    }

    WindowHandler* windowHandler = WindowHandler::instance();
    windowHandler->project = project;

#ifndef __EMSCRIPTEN__
    // External control channel (see ControlServer), configured purely from
    // settings.json (`controlEnabled` / `controlSocketPath`).
    if (Settings::instance().controlEnabled())
        ControlServer::instance().start(Settings::instance().controlSocketPath());
#endif

    if (!audioManager->start())
        std::cout << "[Audio] failed to start, running without audio" << std::endl;

    project->setup();

#ifdef __EMSCRIPTEN__
    static Project* g_project = project;
    emscripten_set_main_loop([]() {
        g_project->processor->setThreadActiveRoot(g_project->processor->guiGraph);
        WindowHandler::instance()->tick();
    }, 0, 1);
#else
    while (windowHandler->tick() && !quit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
#endif

    project->save();
#ifndef __EMSCRIPTEN__
    ControlServer::instance().stop();
#endif
    audioManager->stop();
    delete project;
    SDL_Quit();

    return 0;
}
