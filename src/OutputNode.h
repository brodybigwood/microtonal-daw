#pragma once

#include "Node.h"
#include <array>

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
        bool blocksDoubleClick(float, float) const override;

        void deSerialize(json);
        json serialize();

        void setCoupledPatcher(PatcherNode*);
        void placeDefaultByWindowSize(float windowW, float windowH);

        size_t countWaveformInputs() const;
        size_t totalWaveformChannels() const;

        void addWaveformInputChannel();
        void removeLastWaveformInputChannel();
        bool removeWaveformInputById(uint16_t id);
        void insertWaveformInputChannelAt(size_t index, uint16_t id);
        bool peekLastRemovableWaveformInput(uint16_t* outId, size_t* outIndex) const;

        void addEventInputSocket();
        void removeLastEventInputSocket();
        bool removeEventInputById(uint16_t id);
        void insertEventInputChannelAt(size_t index, uint16_t id);
        bool peekLastRemovableEventInput(uint16_t* outId, size_t* outIndex) const;

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
        size_t countLocalEventInputs() const;

        PatcherNode* coupledPatcher = nullptr;
        bool shouldAutoPlaceFromWindow = true;
};
