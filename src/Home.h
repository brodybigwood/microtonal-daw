
#include <SDL_ttf.h>
#include "Region.h"
#include "Note.h"
#include "Button.h"
#include <vector>
#include "WindowHandler.h"

#ifndef HOME_H
#define HOME_H

class WindowHandler;  // Forward declaration

class Home {

    public:


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
};

#endif