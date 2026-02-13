#pragma once
#include <vector>
#include "fract.h"
#include <SDL3/SDL.h>
class fract;
#include "idManager.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

enum ElementType{
    region = 0,
    audioClip = 1
};

class GridElement {
    public:

        GridElement();
        virtual ~GridElement();

        struct Position {
            fract startOffset;
            fract start;
            fract end;
            fract length;
            uint16_t trackID;
            int id;
        };

        ElementType type;

        void createPos(fract, uint16_t);

        std::vector<Position> positions;

        SDL_Texture* texture = nullptr;

        virtual void draw(SDL_Renderer*, float, int) = 0;
        float pixelsPerSecond = 0;
        int h = 0;

        virtual json toJSON();
        virtual void fromJSON(json);

        static idManager* id_pool();

        uint16_t id;
};
