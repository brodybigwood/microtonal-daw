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

        json extraSerialize() override;
        void extraDeSerialize(const json&) override;

    private:
        void rebuildState(json);
        void ensureSongRoll();
        void syncSongRollContext();
};
