#include <SDL3/SDL.h>
#include <SDL_ttf.h>

#ifndef CONTROLAREA_H
#define CONTROLAREA_H

class ControlArea {

    public:
        ControlArea(int x, int y, int width, int height, SDL_Renderer* renderer);
        ~ControlArea();
        
        SDL_Renderer* renderer;
        int x;
        int y;
        int width;
        int height;
};

#endif