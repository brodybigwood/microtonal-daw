#pragma once
#include <vector>
#include "fract.h"
#include <SDL3/SDL.h>
class fract;
#include "idManager.h"

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
            uint16_t trackID;
            int id;
        };

        void createPos(fract, uint16_t);

        std::vector<Position> positions;

        SDL_Texture* texture = nullptr;

        virtual void draw(SDL_Renderer*) = 0;

        json toJSON();
        void fromJSON(json);

        static idManager* id_pool();
};
