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

        if (ctxMenu->active) home->render(); // need to render behind ctxmenu first

        static SDL_Event motion;
        if (motion.type != 0) {
            if (ctxMenu->active) ctxMenu->tick(motion);
            else if (!home->handleInput(motion)) return false;
        }
        SDL_zero(motion);

        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                if (motion.type == 0) { //
                    motion.type = SDL_EVENT_MOUSE_MOTION;
                    motion.motion.windowID = e.motion.windowID; // the accumulated motion will be for this window
                }
                if (motion.motion.windowID == e.motion.windowID) { // if the new event is for the accumulated event's window, accumulate it
                    motion.motion.xrel += e.motion.xrel;
                    motion.motion.yrel += e.motion.yrel;
                } else { // if not, handle the accumulated motion for the previous window and then reset it
                    if (ctxMenu->active) ctxMenu->tick(motion);
                    else if (!home->handleInput(motion)) return false;
                    SDL_zero(motion);
                    motion.type = SDL_EVENT_MOUSE_MOTION; // accumulate the motion for the new window 
                    motion.motion.windowID = e.motion.windowID;
                    motion.motion.xrel += e.motion.xrel;
                    motion.motion.yrel += e.motion.yrel;
                }
                continue; // e already saved or handled
            } else if (motion.type != 0) { // if there is a non motion event but there is previous motion, handle the previous motion first
                if (ctxMenu->active) ctxMenu->tick(motion);
                else if (!home->handleInput(motion)) return false;
                SDL_zero(motion);
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                SDL_Window* clickedWindow = SDL_GetWindowFromID(e.button.windowID);
                if (clickedWindow) {
                    SDL_RaiseWindow(clickedWindow);
                }
            }

            toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
            toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
            toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);

            if (ctxMenu->active) ctxMenu->tick(e);
            else if (!home->handleInput(e)) return false;
        }
        
        if (!ctxMenu->active) home->render(); // render on top if ctxmenu not open
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

