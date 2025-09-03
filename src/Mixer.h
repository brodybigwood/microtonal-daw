#pragma once

#include <SDL3/SDL.h>
#include "Project.h"

class Mixer {
    public:
        Mixer();
        ~Mixer();

        std::vector<MixerTrack*>* tracks;
    
        SDL_FRect* dstRect;
        SDL_Renderer* renderer;

        void tick();
};
