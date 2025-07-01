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
    home->createRoll(0);
}


WindowHandler::~WindowHandler() {

}

WindowHandler* WindowHandler::instance() {
    static WindowHandler w;
    return &w;
}

void WindowHandler::createPianoRoll(DAW::Region* region, SDL_FRect* pRect) {
    std::cout<<"creating"<<std::endl;

    editor = new PianoRoll(pRect, region, false);

    SDL_SetWindowParent(editor->window, home->window);

    windows->push_back(editor);
}

bool WindowHandler::tick() {



    bool running = true;



                
        double timeSinceLastFrame = double(SDL_GetTicks())-double(lastTime);
        if(timeSinceLastFrame >= frameTime) {
            lastTime = double(SDL_GetTicks())-frameTime;
            home->tick();
            for(int i = windows->size() - 1; i >= 0; --i) {

                    (*windows)[i]->tick();
                
            }

            SDL_RenderPresent(renderer);

            PluginManager::instance().tickAll();

            while (SDL_PollEvent(&e)) {
                
                switch (e.type) {
                            case SDL_EVENT_QUIT:
                            running = false;
                            break;
                            
                            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                            {
                                // Find the window that triggered the close request
                                PianoRoll* window = findWindow();

                                if(window == nullptr) {
                                    running = false;
                                    break;
                                }
                                
                                if (window) {
                                    std::cout << "Window found. Closing it..." << std::endl;
                                    
                                    // Clean up the window (this should call the destructor and free resources)
                                    delete window;
                                    
                                    // Optional: Remove the window from the vector of active windows
                                    auto it = std::find(windows->begin(), windows->end(), window);
                                    if (it != windows->end()) {
                                        windows->erase(it);
                                    }
                                } else {
                                    std::cout << "No window found for close request." << std::endl;
                                }
                                
                                break;
                            }
                            





                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        handleMouse();
                        break;
                    default:
                    handleKeyboard();

                        break;
                }
            }

        }
        return running;

}


bool WindowHandler::handleKeyboard() {
    
    focusedWindow = SDL_GetKeyboardFocus();
    if (focusedWindow != nullptr) {

        if(focusedWindow == home->window) {
            
            return home->handleInput(e);
        } else {
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

