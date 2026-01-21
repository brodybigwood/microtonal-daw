#include "WindowHandler.h"
#include <X11/Xlib.h>
#include "SDL_Events.h"
WindowHandler::WindowHandler() {

    SDL_SetHint(SDL_HINT_APP_NAME, "EDITOR");
    SDL_SetHint(SDL_HINT_APP_ID, "daw.editor");

    mainWindow = SDL_CreateWindow("Piano Roll", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(mainWindow, NULL);
    
    lastTime = SDL_GetTicks();

    SDL_FRect txtRect{
        windowWidth/2.0f - 40,
        windowHeight/2.0f - 40,
        windowWidth/2.0f + 40,
        windowHeight/2.0f + 40
    };

    txtInput = TextInput::get();

    txtInput->dstRect = txtRect;
    txtInput->renderer = renderer;
    SDL_StartTextInput(mainWindow);
    txtInput->active = false;
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

    double timeSinceLastFrame = double(SDL_GetTicks())-double(lastTime);
    if(timeSinceLastFrame >= frameTime) {
        lastTime = double(SDL_GetTicks())-frameTime;

        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(txtInput->active) {
                txtInput->tick(e);
            } else {
                if( !home->tick(e)) {
                    return false;
                }
            }
        }
        SDL_RenderPresent(renderer);
    }
    return true;

}

