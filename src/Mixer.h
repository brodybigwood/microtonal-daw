#pragma once

#include <SDL3/SDL.h>
#include "Project.h"
#include "gui.h"

class Mixer {
    public:
        Mixer();
        ~Mixer();

        std::vector<MixerTrack*>* tracks;
    
        SDL_FRect* dstRect;
        SDL_Renderer* renderer;

        void tick();

        int create();

        void handleInput(SDL_Event& e);

    private:
        void clickMouse(SDL_Event& e);
        void moveMouse();

        float mouseX;
        float mouseY;
        int scroll;
        
        float trackWidth = 50;

        int hoveredId = -1;
        int selectId = -1;

        void getHoveredTrack();
};
