#include "WindowHandler.h"
#include <X11/Xlib.h>
#include "PluginManager.h"
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

void WindowHandler::createPianoRoll(DAW::Region* region, SDL_FRect* pRect) {
    if(editor) {
        return;
    }
    std::cout<<"creating"<<std::endl;

    editor = new PianoRoll(pRect, region, &(home->pianoRollDetached));

    home->pianoRoll = editor;

    SDL_SetWindowParent(editor->window, home->window);

    windows->push_back(editor);
}

bool WindowHandler::tick() {

    bool running = true;
                
    double timeSinceLastFrame = double(SDL_GetTicks())-double(lastTime);
    if(timeSinceLastFrame >= frameTime) {
        lastTime = double(SDL_GetTicks())-frameTime;

        PluginManager::instance().tickAll();

        running = home->tick();
        SDL_RenderPresent(renderer);
    }
    return running;

}


bool WindowHandler::handleKeyboard() {
    
    focusedWindow = SDL_GetKeyboardFocus();
    if (focusedWindow != nullptr) {

        if(focusedWindow == home->window) {

            return home->handleInput(e);
        } else {
            return false;
            for (PianoRoll* window : *windows) {
                // Assuming that the PianoRoll class has a method to get the SDL_Window* for that window
                if (window->window == focusedWindow) {
                    window->handleInput(e);;
                    return false;
                }
            }
        }
        // Iterate through the windows to find the corresponding PianoRoll object

    }
    return false;
}

bool WindowHandler::handleMouse() {
    
    focusedWindow = SDL_GetMouseFocus();
    if (focusedWindow != nullptr) {

        if(focusedWindow == home->window) {
            return home->handleInput(e);
        } else {
            return false;
        // Iterate through the windows to find the corresponding PianoRoll object
            for (PianoRoll* window : *windows) {
                // Assuming that the PianoRoll class has a method to get the SDL_Window* for that window
                if (window->window == focusedWindow) {
                    window->handleInput(e);
                    return false;
                }
            }
        }
    } 
    return false;
}

PianoRoll* WindowHandler::findWindow() {
    for (PianoRoll* window : *windows) {
        if (SDL_GetWindowID(window->window) == e.window.windowID) {
            return window;
        }
    }
    return nullptr;
}

