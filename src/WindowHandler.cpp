#include "WindowHandler.h"
#include "SDL_Events.h"

WindowHandler::WindowHandler() {

    SDL_SetHint(SDL_HINT_APP_NAME, "EDITOR");
    SDL_SetHint(SDL_HINT_APP_ID, "daw.editor");

    lastTime = SDL_GetTicks();

    ctxMenu = ContextMenu::get();
}

WindowHandler::~WindowHandler() {
}

WindowHandler* WindowHandler::instance() {
    static WindowHandler w;
    return &w;
}

bool WindowHandler::tick() { 

    double timeSinceLastFrame = double(SDL_GetTicks())-double(lastTime);
    if(timeSinceLastFrame >= frameTime) {
        lastTime = double(SDL_GetTicks())-frameTime;

        if (ctxMenu->active) project->render(); // need to render behind ctxmenu first
        bool eventHandled = false;
 
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            eventHandled = true;

            if(!ctxMenu->active && e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_SPACE) {
                project->togglePlaying();
                continue;
            }


            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                SDL_Window* clickedWindow = SDL_GetWindowFromID(e.button.windowID);
                if (clickedWindow) {
                    SDL_RaiseWindow(clickedWindow);
                }
            }

            if (isCtrlPressed) {
                if (e.type == SDL_EVENT_KEY_DOWN) {
                    if (e.key.key == SDLK_Z) {
                        if (isShiftPressed) project->redo();
                        else project->undo();
                    } else if (e.key.key == SDLK_S) {
                        project->save();
                    }
                }
            }

            toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
            toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
            toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);

            if (ctxMenu->active) ctxMenu->tick(e);
            else for (auto w : windows)
                if (SDL_GetWindowFromID(getEventWindowID(e)) == w->window) {
                    w->handleWindowInput(e);
                    break;
                }
        }

        if (!eventHandled && ctxMenu->active) { // home was already rendered, but ctxMenu hasn't rendered yet. so render on top by triggering with fake event
            e.type = SDL_EVENT_USER;
            e.window.windowID = ctxMenu->window_id;
            ctxMenu->tick(e);
        } else if (!ctxMenu->active) {
            project->render();
        }
        std::cout << "h" << std::endl;
        project->renderPresent();
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

void WindowHandler::addWindow(Window* w) {
    windows.push_back(w);
}

void WindowHandler::removeWindow(Window* w) {
    auto it = std::find(windows.begin(), windows.end(), w);
    if (it != windows.end()) {
        windows.erase(it);
    }
}
