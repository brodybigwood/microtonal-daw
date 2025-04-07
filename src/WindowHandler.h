#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#ifndef WINDOWHANDLER_H
#define WINDOWHANDLER_H
#include <vector>
#include "PianoRoll.h"
#include "Region.h"
#include "Home.h"
#include <thread>
#include <algorithm>

class WindowHandler {
    public:
    SDL_Window* focusedWindow;
std::vector<PianoRoll*>* windows = new std::vector<PianoRoll*>();
SDL_Event e;
        WindowHandler();
        ~WindowHandler();

        void handleKeyboard();
        void handleMouse();

        Region midiRegion1;
        Region midiRegion2;
        Home home;
        PianoRoll* editor;
        PianoRoll* findWindow();
        void loop();
};

#endif