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

    ctxMenu = ContextMenu::get();
}

void WindowHandler::createHome(Project* project) {
    home = Home::get();
    home->createRoll();
}


WindowHandler::~WindowHandler() {
    if (home->pianoRoll) delete home->pianoRoll;
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

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                SDL_Window* clickedWindow = SDL_GetWindowFromID(e.button.windowID);
                if (clickedWindow) {
                    SDL_RaiseWindow(clickedWindow);
                }
            }

            toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
            toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
            toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);

            if (ctxMenu->active) {
                SDL_Event a{
                    .type = SDL_EVENT_MOUSE_MOTION
                };
                
                if (!home->tick(a)) return false;
                ctxMenu->tick(e);
                SDL_RenderPresent(ctxMenu->renderer);
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

void WindowHandler::toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar) {

    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        if (e.key.scancode == keycode) {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                keyVar = true;
            } else if (e.type == SDL_EVENT_KEY_UP) {
                keyVar = false;
            }
        }
    }
}

