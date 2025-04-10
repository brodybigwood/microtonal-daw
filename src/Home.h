
#include <SDL_ttf.h>
#include <SDL3/SDL.h>
#include "Button.h"
#include <vector>
#include "WindowHandler.h"
#include "InstrumentList.h"
#include "ControlArea.h"
#include "SongRoll.h"
#include "Project.h"

#ifndef HOME_H
#define HOME_H

class WindowHandler;  // Forward declaration

class Home {

    public:

    int windowHeight = 1080;
    int windowWidth = 1920;

    int controlsHeight = 50;

    int instWidth = 200;

    int mixerHeight = 200;

    SongRoll* song;
    
    InstrumentList* insts;



        Home(Project* project, WindowHandler* windowHandler);
        ~Home();

        Project* project;

        WindowHandler* windowHandler;
        SDL_Window* window;
        SDL_Renderer* renderer;

        void tick();

        bool handleInput(SDL_Event& e);

        float mouseX, mouseY;

        void hoverButtons();

        std::vector<Button> buttons;

        Button* hoveredButton = nullptr;

        ControlArea* controls;

        bool mouseOnSong();


};

#endif