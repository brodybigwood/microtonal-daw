
#include <SDL_ttf.h>
#include "Region.h"
#include "Note.h"
#include "Button.h"
#include <vector>
#include "WindowHandler.h"
#include "InstrumentList.h"
#include "ControlArea.h"
#include "SongRoll.h"

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

    Region midiRegion1;
Region midiRegion2;

        Home(WindowHandler* windowHandler);
        ~Home();

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


};

#endif