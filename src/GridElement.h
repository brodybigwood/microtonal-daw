#pragma once
#include <vector>
#include <utility>
#include <SDL3/SDL.h>
#include "idManager.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

enum ElementType{
    region = 0,
    audioClip = 1,
    automationCurve = 2
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
            std::vector<std::pair<int, int>> rhythmVector;
            std::vector<std::pair<int, int>> rhythmEndVector;
            std::vector<std::pair<int, int>> startOffsetPairs;
            uint16_t trackID;
            int id;
            GridElement* element = nullptr;
            int rhythmEdoSubdivisionSteps = 1;
            std::vector<std::pair<int, int>> rhythmEdoLowerVector;
            std::vector<std::pair<int, int>> rhythmEdoUpperVector;
        };

        ElementType type;

        void createPos(std::vector<std::pair<int, int>> startPairs, std::vector<std::pair<int, int>> endPairs, uint16_t,
                       int rhythmEdoSteps, std::vector<std::pair<int, int>> rhythmEdoLower, std::vector<std::pair<int, int>> rhythmEdoUpper,
                       std::vector<std::pair<int, int>> startOffsetPairs = {});

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
