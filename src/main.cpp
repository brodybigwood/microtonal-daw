#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"

#include <thread>
#include <csignal>


int main() {

    std::signal(SIGINT, [](int){ exit(130); });
    
    if(!initFonts()) {
        std::cerr << "ttf init failed failed" << std::endl;
        return -1;
    }

    if(!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "sdl init failed failed" << std::endl;
        return -1;
    }

    createCursors();

    Project* project = Project::instance();
    AudioManager* audioManager = AudioManager::instance();

    audioManager->setProject(project);

    WindowHandler* windowHandler = WindowHandler::instance();

    windowHandler->createHome(project);

    project->load("test");

    if (!audioManager->start()) {
        std::cout << "audiomanager failed" << std::endl;
        return -1;
    }

    project->setup();


    while (windowHandler->tick()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 1000ms / 200Hz = 5ms
    }

    audioManager->stop();
    project->save();

    return 0;
}
