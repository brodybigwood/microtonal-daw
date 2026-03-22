#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"
#include "styles.h"

#include <thread>
#include <csignal>


int main(int argc, char* argv[]) {

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
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 1000ms / 200Hz = 5ms
    }

    SDL_Quit();

    audioManager->stop();
    project->save();

    delete audioManager;
    delete project;

    return 0;
}
