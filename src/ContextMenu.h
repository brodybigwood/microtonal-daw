#pragma once

#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <functional>
#include <string>
#include "TreeEntry.h"

class ContextMenu {

    public:

        float locX;
        float locY;

        bool active = false;

        static ContextMenu* get();
        
        SDL_Renderer* renderer;        
       
        uint32_t window_id;
    
        void tick(SDL_Event& e);

        std::function<bool(SDL_Event& e)> dynamicTick;
};

std::function<bool(SDL_Event& e)> getTextInputTicker(std::function<void(std::string text)> enter);

std::function<bool(SDL_Event& e)> getTreeMenuTicker(std::shared_ptr<TreeEntry> t);
