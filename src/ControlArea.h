#include <SDL3/SDL.h>
#include <SDL_ttf.h>

#ifndef CONTROLAREA_H
#define CONTROLAREA_H

class ControlArea {

    public:
        ControlArea(int x, int y, int width, int height);
        ~ControlArea();
        
        int x;
        int y;
        int width;
        int height;
};

#endif