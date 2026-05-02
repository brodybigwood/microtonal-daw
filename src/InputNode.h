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

        /** Graph edits (IoPortChannelAction / NodeManager). */
        void addWaveformOutputChannel();
        void removeLastWaveformOutputChannel();
        bool removeWaveformOutputById(uint16_t id);
        void insertWaveformOutputChannelAt(size_t index, uint16_t id);
        bool peekLastRemovableWaveformOutput(uint16_t* outId, size_t* outIndex) const;

        void addEventOutputSocket();
        void removeLastEventOutputSocket();
        bool removeEventOutputById(uint16_t id);
        void insertEventOutputChannelAt(size_t index, uint16_t id);
        bool peekLastRemovableEventOutput(uint16_t* outId, size_t* outIndex) const;

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
        size_t countLocalEventOutputs() const;

        PatcherNode* coupledPatcher = nullptr;
        bool shouldAutoPlaceFromWindow = true;
};
