

#include "WindowHandler.h"
#include "Project.h"



int main() {

    Project* project = new Project();
    

    WindowHandler gui(project);

    gui.loop();

}



