#pragma once

#include "Bus.h"
#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

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

        void setType();

        void setBus(uint8_t);
        uint16_t id;

        void process(float* input, int bufferSize);

        std::vector<Event> events;

        float* buffer;
        int bufferSize;

        uint8_t inputChannel;

        void render(SDL_Renderer*, SDL_FRect&);
        void handleInput(SDL_Event&);

        void fromJSON(json);
        json toJSON();

};
