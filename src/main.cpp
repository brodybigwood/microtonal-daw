#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"
#include "NodeProcessor.h"
#include "ControlServer.h"
#include "Settings.h"
#include "LoadingScreen.h"
#include "styles.h"

#ifndef __EMSCRIPTEN__
#include "nodes/vst/vstplugin.h"
#endif

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

#ifndef __EMSCRIPTEN__
    initVstX11Threads();   // before SDL_Init touches Xlib
#endif

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
#ifndef __EMSCRIPTEN__
        // Load on a worker so the window can show the animated loading screen;
        // the main thread only draws until the project is ready.
        std::atomic<bool> loadDone{false};
        std::thread loadThread([&] {
            // Undo deserialization resolves graphs through the thread-local
            // active manager; it is per-thread, so set it for the loader too.
            NodeProcessor::setThreadActiveRoot(project->processor->guiGraph);
            try {
                project->load(argv[1]);
            } catch (const std::exception& e) {
                std::cerr << "[load] failed: " << e.what() << std::endl;
            }
            loadDone.store(true);
        });
        const Uint64 startTicks = SDL_GetTicks();
        while (!loadDone.load() && !quit.load()) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) quit.store(true);
            }
            int ww = 0, hh = 0;
            SDL_GetWindowSize(project->window, &ww, &hh);
            SDL_SetRenderDrawColor(project->renderer, 0, 0, 0, 255);
            SDL_RenderClear(project->renderer);
            const char* stage = project->loadStatusText.load();
            loading::render(project->renderer, ww, hh,
                            static_cast<double>(SDL_GetTicks() - startTicks) / 1000.0,
                            project->loadProgressValue.load(), stage ? stage : "");
            SDL_RenderPresent(project->renderer);
            SDL_Delay(16);
        }
        loadThread.join();
        // The loader replaced guiGraph on its own thread; this thread's
        // thread-local manager still points at the deleted graph.
        NodeProcessor::setThreadActiveRoot(project->processor->guiGraph);
#else
        project->load(argv[1]);
#endif
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
    project->waitForSave(); // the save worker is joinable; exit() would kill it mid-write
#ifndef __EMSCRIPTEN__
    ControlServer::instance().stop();
#endif
    audioManager->stop();
    delete project;
    SDL_Quit();

    return 0;
}
