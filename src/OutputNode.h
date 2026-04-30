#pragma once

#include "Node.h"

class OutputNode : public Node {
    public:

        OutputNode(NodeManager*);

        float* output;

        int numChannels;

        void process() override;
        void renderContent(SDL_Renderer*) override;
        void handleWindowInput(SDL_Event&) override;

        void setup() override;

        void deSerialize(json);
        json serialize();

    private:
        SDL_FRect addRect{20, 20, 48, 32};
        SDL_FRect removeRect{76, 20, 48, 32};
        void addChannel();
        void removeChannel();
};
