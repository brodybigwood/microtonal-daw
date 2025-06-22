#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include "Instrument.h"
#include <vector>
#include <iostream>
#include "Project.h"
#include "InstrumentMenu.h"

#ifndef INSTRUMENTLIST_H
#define INSTRUMENTLIST_H

class InstrumentList {

    public:
    InstrumentList(int y, int width, int height, SDL_Renderer* renderer, Project* project);
        ~InstrumentList();

        Project* project;

        int height;

        int width;

        int y;

        int instrumentHeight = 50;
        
        SDL_Renderer* renderer;

        SDL_Texture* noteTexture;
        void render();

        SDL_Texture* texture;

        SDL_FRect dstRect;

        void renderInstrument(int i);

        int hoveredInstrument = -1;

        void moveMouse(float, float);

        void clickMouse(SDL_Event& e);

        float mouseX;
        float mouseY;

        bool lmb, rmb;

        void getHoveredInstrument();
};

#endif
