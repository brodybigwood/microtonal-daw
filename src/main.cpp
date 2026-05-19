#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"
#include "styles.h"

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

    if (!audioManager->start()) {
        std::cout << "audiomanager failed" << std::endl;
        return -1;
    }

    project->setup();

    while (windowHandler->tick()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    project->save();
    audioManager->stop();

    delete audioManager;
    delete project;
    SDL_Quit();

    return 0;
}
