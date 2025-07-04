#pragma once
#include <vector>
#include "fract.h"
#include <SDL3/SDL.h>
class fract;
class Instrument;

class GridElement {
    public:

        GridElement();
        ~GridElement();

        struct Position {
            fract startOffset;
            fract start;
            fract end;
            fract length;
            Instrument* instrument;
            int id;
        };

        void createPos(fract, Instrument*);

        std::vector<Position> positions;

        SDL_Texture* texture = nullptr;

        virtual void draw(SDL_Renderer*) = 0;
};
