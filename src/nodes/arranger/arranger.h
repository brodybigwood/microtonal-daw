#pragma once

#include "Node.h"
#include "SongRoll.h"

class TrackManager;
class ElementManager;

class ArrangerNode : public Node {
    public:
        ArrangerNode(uint16_t, NodeManager*);
        ~ArrangerNode() override;
        void process() override;
        void setup() override;
        bool handleCustomInput(SDL_Event&) override;
        void renderContent(SDL_Renderer*) override;

        void clearCustomTextures() override;

        SDL_FRect* slRect;
        SongRoll* sl = nullptr;

        TrackManager* tracks = nullptr;
        ElementManager* elements = nullptr;

        // Rhythm EDO — same model as Region
        int rhythmEdoSubdivisionSteps = 1;
        std::vector<std::pair<int, int>> rhythmEdoLowerVector; // default empty = time 0
        std::vector<std::pair<int, int>> rhythmEdoUpperVector = {{1,1}}; // default = 1 second

        json extraSerialize() override;
        void extraDeSerialize(const json&) override;

    private:
        void rebuildState(json);
        void syncSongRollContext();

    public:
        void ensureSongRoll();
};
