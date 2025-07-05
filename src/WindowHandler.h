#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#ifndef WINDOWHANDLER_H
#define WINDOWHANDLER_H
#include <vector>
#include "PianoRoll.h"
#include "Home.h"
#include <thread>
#include <algorithm>
#include "Project.h"

class WindowHandler {
    public:
    SDL_Window* focusedWindow;
std::vector<SDL_Window*> windows;
SDL_Event e;
        WindowHandler();
        ~WindowHandler();

        void createHome(Project* project);

        SDL_Renderer* renderer;

        static WindowHandler* instance();

        bool handleKeyboard();
        bool handleMouse();

        Project* project;

        Home* home;
        PianoRoll* editor;
        PianoRoll* findWindow();
        bool tick();

        void createPianoRoll(std::shared_ptr<DAW::Region>, SDL_FRect*);


        Uint32 lastTime;

        double fps = 60;
    
        double frameTime = 1000/fps;   
    
        SDL_Window* mainWindow;

        int windowWidth = 1920;
        int windowHeight = 1080;


};

#endif
