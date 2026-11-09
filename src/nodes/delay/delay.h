#pragma once

#include "Node.h"
#include <vector>

class NodeEditor;

class DelayNode : public Node {
    public:
        DelayNode(uint16_t, NodeManager*);
        ~DelayNode() override;

        void process() override;
        void setup() override;
        bool widthFollowsInput() const override { return true; }

        json extraSerialize() override;
        void extraDeSerialize(const json&) override;

        void renderContent(SDL_Renderer*) override;
        bool handleCustomInput(SDL_Event&) override;
        void clearCustomTextures() override;

        // Node graph in the feedback path: its input is (input + previous output * feedback).
        NodeManager* chain = nullptr;
        NodeEditor* chainEditor = nullptr;

        // Node graph processed on the input before it enters the delay line.
        // Its managerPath is [nodeID, -1] so it is distinct from the feedback
        // graph's [nodeID].
        NodeManager* preChain = nullptr;
        NodeEditor* preEditor = nullptr;

    private:
        Connection* in = nullptr;
        Connection* out = nullptr;

        Knob timeKnob = Knob(0.25f, 40.0f, 28.0f, 11.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Time");
        Knob feedbackKnob = Knob(0.5f, 100.0f, 28.0f, 11.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Feedback");
        Knob mixKnob = Knob(0.5f, 160.0f, 28.0f, 11.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Mix");

        void ensureCapacity(int channels);

        // Per-channel circular delay line.
        std::vector<std::vector<float>> line;
        std::vector<float> prevBuf;
        std::vector<float> sumBuf;
        std::vector<float> chainBuf;
        std::vector<float> preBuf;
        int lineLen = 0;
        int writePos = 0;
        int lineChannels = 0;
};
