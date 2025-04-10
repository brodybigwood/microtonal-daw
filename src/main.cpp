/*
  ==============================================================================

    main.cpp
    Created: 10 Apr 2025 2:04:22am
    Author:  brody

  ==============================================================================
*/


#include "WindowHandler.h"
#include "Project.h"



int main() {

    Project* project = new Project();
    

    WindowHandler gui(project);

    gui.loop();

}