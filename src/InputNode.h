#pragma once

#include "Node.h"
#include <array>
class PatcherNode;

class InputNode : public Node {
    public:

        InputNode(NodeManager*);

        float* input;

        int numChannels;

        void process() override;
        void renderContent(SDL_Renderer*) override;
        void handleWindowInput(SDL_Event&) override;

        void setup() override;
        bool blocksDoubleClick(float, float) const override;

        void deSerialize(json);
        json serialize();

        void setCoupledPatcher(PatcherNode*);
        void placeDefaultByWindowSize(float windowW, float windowH);

        size_t countWaveformOutputs() const;

    private:
        SDL_FRect addRect{20, 20, 48, 32};
        SDL_FRect removeRect{76, 20, 48, 32};
        SDL_FRect evAddRect{20, 56, 48, 32};
        SDL_FRect evRemoveRect{76, 56, 48, 32};
        using Quad = std::array<SDL_FPoint, 4>;
        Quad addQuad{};
        Quad removeQuad{};
        Quad evAddQuad{};
        Quad evRemoveQuad{};
        void addChannel();
        void removeChannel();
        void addEventOutputChannel();
        void removeLastEventOutputChannel();
        size_t countLocalEventOutputs() const;

        PatcherNode* coupledPatcher = nullptr;
        bool shouldAutoPlaceFromWindow = true;
};
