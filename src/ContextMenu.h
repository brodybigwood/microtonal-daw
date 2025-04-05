#include <SDL3/SDL.h>
#include <SDL_ttf.h>

#ifndef CONTEXTMENU_H
#define CONTEXTMENU_H

class ContextMenu {

    public:
        ContextMenu();
        ~ContextMenu();
        
        void render(SDL_Renderer* renderer);  // Add method to render the context menu
        void handleInput(SDL_Event& e);  // Handle input like clicks, etc.

};

#endif