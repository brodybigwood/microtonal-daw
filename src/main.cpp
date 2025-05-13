#include "Project.h"
#include "WindowHandler.h"
#include <thread>
#include <RtAudio.h>


int main() {
    Project* project = new Project();
    WindowHandler* windowHandler = new WindowHandler(project);
    
        while (windowHandler->tick()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 1000ms / 200Hz = 5ms
        }
}
