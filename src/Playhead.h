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
        Playhead(SDL_FRect*, SDL_FRect*, bool*, fract* startTime);
        ~Playhead();

        bool* detached;

        Project* project;
        WindowHandler* windowHandler;
        fract pos = fract(0,1);

        void render(SDL_Renderer* renderer, float barWidth, float offset);

        void setTime(fract);

        float timePx;
        fract* startTime;

        void getTimePx(float barWidth);

        SDL_FRect* gridRect;
        SDL_FRect* dstRect;
};

#endif
