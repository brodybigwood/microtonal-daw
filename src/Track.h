#pragma once

#include "Bus.h"
#include <SDL3/SDL.h>

enum TrackType{
    Audio = 0,
    Automation = 1,
    Notes = 2
};

class Track {
    public:

        int busID = -1;
        Bus* dstBus;

        TrackType type = TrackType::Notes;
        TrackType& getType();

        void setBus(uint8_t);

        void process();

        uint16_t inputChannel;

        void render(SDL_Renderer*, SDL_FRect&);
        void handleInput(SDL_Event&);

};
