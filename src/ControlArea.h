#include <SDL3/SDL.h>
#include <SDL_ttf.h>

#ifndef CONTROLAREA_H
#define CONTROLAREA_H

class ControlArea {

    public:
        ControlArea(int height, int width, SDL_Renderer* renderer);
        ~ControlArea();
        
        SDL_Renderer* renderer;
        int x = 0;
        int y = 0;
        int width;
        int height;

        void render();

        SDL_Texture* texture;

        SDL_FRect dstRect;

};

#endif