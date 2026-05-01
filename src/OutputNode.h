#pragma once

#include "Node.h"

class PatcherNode;

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

        void setCoupledPatcher(PatcherNode*);

        size_t countWaveformInputs() const;

    private:
        SDL_FRect addRect{20, 20, 48, 32};
        SDL_FRect removeRect{76, 20, 48, 32};
        SDL_FRect evAddRect{20, 56, 48, 32};
        SDL_FRect evRemoveRect{76, 56, 48, 32};
        void addChannel();
        void removeChannel();
        void addEventOutputChannel();
        void removeLastEventOutputChannel();
        size_t countLocalEventInputs() const;

        PatcherNode* coupledPatcher = nullptr;
};
