#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"
#include "EventManager.h"

#include <thread>



int main() {
    
    if(!initFonts()) {
        return -1;
    }

    Project* project = Project::instance();
    AudioManager* audioManager = AudioManager::instance();

    audioManager->setProject(project);

    WindowHandler* windowHandler = new WindowHandler(project);

    EventManager* eventManager = new EventManager(project);



    if (!audioManager->start()) {
        std::cout << "audiomanager failed" << std::endl;
        return -1;
    }
    project->load();

    while (windowHandler->tick()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 1000ms / 200Hz = 5ms
    }
}
