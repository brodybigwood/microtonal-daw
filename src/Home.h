
#include <SDL_ttf.h>
#include <SDL3/SDL.h>
#include "Button.h"
#include <vector>
#include "WindowHandler.h"
#include "InstrumentList.h"
#include "SongRoll.h"
#include "Project.h"
#include "InstrumentMenu.h"
#include "Instrument.h"

#ifndef HOME_H
#define HOME_H

class WindowHandler;  // Forward declaration

class Home {

    public:

    SDL_FRect pianoRollRect = {200,200,800,600};
    PianoRoll* pianoRoll = nullptr;

    int windowHeight = 1080;
    int windowWidth = 1920;

    int controlsHeight = 50;
    int mixerHeight = 200;

    int instMenuWidth = 200;

    bool mouseOn(SDL_FRect*);

    bool sendInput(SDL_Event&);

    SDL_FRect songRect;
    SongRoll* song;

    bool pianoRollDetached = true;
    bool songRollDetached = true;
    
    InstrumentList* insts;

        static Home* get();

        void createRoll();

        Home(Project* project);
        ~Home();

        Project* project;

        WindowHandler* windowHandler;
        SDL_Window* window;
        SDL_Renderer* renderer;

        SDL_Texture* instrumentMenuTexture;

        InstrumentMenu* instrumentMenu;

        bool tick();

        bool handleInput(SDL_Event& e);

        float mouseX, mouseY;

        void hoverButtons();

        std::vector<Button> buttons;

        Button* hoveredButton = nullptr;

        bool mouseOnInst();

        bool mouseOnEditor();

};

#endif
