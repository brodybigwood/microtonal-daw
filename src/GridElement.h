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

class Project;
class ArrangerNode;

class GridElement {
    public:
    
        Project* project;
        ArrangerNode* parentNode;

        GridElement(Project*, ArrangerNode*);
        virtual ~GridElement();

        struct Position {
            fract startOffset;
            fract start;
            fract end;
            fract length;
            uint16_t trackID;
            int id;
            GridElement* element = nullptr;
        };

        ElementType type;

        void createPos(fract, uint16_t);

        static json positionToJson(const Position& pos);
        static void applyPositionFromJson(Position* pos, const json& j);
        /** Returns false if id not found. Optionally returns list index before removal (for undo). */
        bool removePositionById(int positionId, size_t* removedIndex = nullptr);
        void insertPositionAt(size_t index, const json& posJson);

        std::vector<Position*> positions;

        SDL_Texture* texture = nullptr;

        virtual void draw(SDL_Renderer*, float, int) = 0;
        float pixelsPerSecond = 0;
        int h = 0;

        virtual json toJSON();
        virtual void fromJSON(json);

        idManager* pos_id_pool = nullptr;

        uint16_t id;
};
