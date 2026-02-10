
#include <SDL_ttf.h>
#include <SDL3/SDL.h>
#include "Button.h"
#include <vector>
#include "WindowHandler.h"
#include "SongRoll.h"
#include "Project.h"

#ifndef HOME_H
#define HOME_H

class WindowHandler;  // Forward declaration

class Home {

    public:

    SDL_FRect pianoRollRect = {200,200,800,600};
    PianoRoll* pianoRoll = nullptr;

    int windowHeight = 1080;
    int windowWidth = 1920;

    bool mouseOn(SDL_FRect*);

    bool sendInput(SDL_Event&);

    SDL_FRect songRect;
    SongRoll* song;

    bool pianoRollDetached = true;
    bool songRollDetached = false;
    
        static Home* get();

        void createRoll();

        Home(Project* project);
        ~Home();

        Project* project;

        WindowHandler* windowHandler;
        SDL_Window* window;
        SDL_Renderer* renderer;

        void render();

        bool handleInput(SDL_Event& e);

        float mouseX, mouseY;

        std::vector<Button> buttons;

        Button* hoveredButton = nullptr;

        bool mouseOnEditor();

};

#endif
