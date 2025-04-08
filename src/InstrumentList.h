#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include "Instrument.h"
#include <vector>
#include <iostream>

#ifndef INSTRUMENTLIST_H
#define INSTRUMENTLIST_H

class InstrumentList {

    public:
    InstrumentList(int height, SDL_Renderer* renderer);
        ~InstrumentList();

        int height;

        int width = 200;

        int instrumentHeight = 50;
        
        SDL_Renderer* renderer;

        SDL_Texture* noteTexture;
        void render();

        SDL_Texture* texture;

        SDL_FRect dstRect;

        std::vector<Instrument*> instruments;

        void renderInstrument(int i);
};

#endif