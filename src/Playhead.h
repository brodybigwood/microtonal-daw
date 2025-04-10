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
        Playhead(SDL_Renderer* renderer, 
            SDL_Texture* ntexture, 
            Project* project, 
            WindowHandler* windowHandler
        );
        ~Playhead();

        SDL_Texture* ntexture;
        SDL_Renderer* renderer;
        Project* project;
        WindowHandler* windowHandler;
        fract pos = fract(0,1);

        void render(float barWidth);

        void setTime(fract);

        float timePx;

        void getTimePx(float barWidth);
        float width, height;
};

#endif