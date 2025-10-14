#include "WindowHandler.h"
#include <X11/Xlib.h>
#include "SDL_Events.h"
WindowHandler::WindowHandler() {

    SDL_SetHint(SDL_HINT_APP_NAME, "EDITOR");
    SDL_SetHint(SDL_HINT_APP_ID, "daw.editor");

    mainWindow = SDL_CreateWindow("Piano Roll", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(mainWindow, NULL);
    
    lastTime = SDL_GetTicks();

}

void WindowHandler::createHome(Project* project) {
    home = Home::get();
    home->createRoll();
}


WindowHandler::~WindowHandler() {

}

WindowHandler* WindowHandler::instance() {
    static WindowHandler w;
    return &w;
}

void WindowHandler::createPianoRoll(Region* region, SDL_FRect* pRect) {
    if(home->pianoRoll) {
        return;
    }
    std::cout<<"creating"<<std::endl;

    home->pianoRoll = new PianoRoll(pRect, region, &(home->pianoRollDetached));
}

bool WindowHandler::tick() {

    bool running = true;
                
    double timeSinceLastFrame = double(SDL_GetTicks())-double(lastTime);
    if(timeSinceLastFrame >= frameTime) {
        lastTime = double(SDL_GetTicks())-frameTime;
        running = home->tick();
        SDL_RenderPresent(renderer);
    }
    return running;

}

