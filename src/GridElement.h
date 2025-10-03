#pragma once
#include <vector>
#include "fract.h"
#include <SDL3/SDL.h>
class fract;

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class GridElement {
    public:

        GridElement();
        ~GridElement();

        struct Position {
            fract startOffset;
            fract start;
            fract end;
            fract length;
            int bus;
            int id;
        };

        void createPos(fract, int);

        std::vector<Position> positions;

        SDL_Texture* texture = nullptr;

        virtual void draw(SDL_Renderer*) = 0;

        json toJSON();
        void fromJSON(json);
};
