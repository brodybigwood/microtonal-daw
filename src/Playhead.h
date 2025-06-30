#include "Project.h"
#include <iostream>
#include "fract.h"
#include "WindowHandler.h"
#include <SDL3/SDL.h>
#include "styles.h"

#ifndef PLAYHEAD_H
#define PLAYHEAD_H
class WindowHandler;
class Playhead {
    public:
        Playhead(Project* project, SDL_FRect* rect);
        ~Playhead();

        Project* project;
        WindowHandler* windowHandler;
        fract pos = fract(0,1);

        void render(SDL_Renderer* renderer, float barWidth, float scrollX);

        void setTime(fract);

        float timePx;

        void getTimePx(float barWidth);
        float* x;
        float* y;
        float* w;
        float* h;
};

#endif
