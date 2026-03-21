#pragma once

#include "Node.h"
#include "SongRoll.h"

class ArrangerNode : public Node {
    public:
        ArrangerNode(uint16_t, NodeManager*);
        void process() override;
        void setup() override;
        bool handleCustomInput(SDL_Event&) override;
        void renderContent(SDL_Renderer*) override;

        void renderPresent() override;
        void clearCustomTextures() override;

        SDL_FRect* slRect;
        bool slDetached = false;
        SongRoll* sl;
};
