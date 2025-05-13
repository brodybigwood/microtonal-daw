#include "Project.h"
#include "WindowHandler.h"
#include "AudioManager.h"

#include <thread>


int main() {
    Project* project = new Project();
    AudioManager* audioManager = new AudioManager(project); 

    WindowHandler* windowHandler = new WindowHandler(project);


    if (!audioManager->start()) {
        std::cout << "audiomanager failed" << std::endl;
        return -1;
    }   
    while (windowHandler->tick()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 1000ms / 200Hz = 5ms
    }
}
