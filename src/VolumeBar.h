#pragma once
#include <SDL3/SDL.h>

class VolumeBar {
    public:
        VolumeBar();
        ~VolumeBar() {};

        enum Type {
            Vertical = 0,
            Horizontal = 0
        };

        Type type;

        float amplitude = 0;

        void render(SDL_Renderer*, SDL_FRect* dstRect);
};
