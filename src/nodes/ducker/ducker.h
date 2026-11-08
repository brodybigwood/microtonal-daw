#pragma once

#include "Node.h"
#include <atomic>

// Sidechain ducker: audio in + key in, one audio out. The key's level drives a
// smooth gain reduction on the audio path (classic kick-triggered pumping).
// No plugin involved, so no editor and no channel/format surprises.
class DuckerNode : public Node {
    public:
        DuckerNode(uint16_t, NodeManager*);

        Connection* in = nullptr;   // audio to duck
        Connection* key = nullptr;  // sidechain trigger
        Connection* out = nullptr;

        void process() override;
        void setup() override;
        bool widthFollowsInput() const override { return true; }
        void renderContent(SDL_Renderer*) override;

        static constexpr float kNODE_H = 160.f;

        // Two rows of two 12px knobs; the labels sit under each knob.
        Knob threshold = Knob(-24.f, NODE_W * 0.30f, 34.f, 12.f, "assets/knobs/1.png", -135.f, 135.f, "Thr dB");
        Knob depth = Knob(0.85f, NODE_W * 0.70f, 34.f, 12.f, "assets/knobs/1.png", -135.f, 135.f, "Depth");
        Knob attack = Knob(8.f, NODE_W * 0.30f, 82.f, 12.f, "assets/knobs/1.png", -135.f, 135.f, "Atk ms");
        Knob release = Knob(180.f, NODE_W * 0.70f, 82.f, 12.f, "assets/knobs/1.png", -135.f, 135.f, "Rel ms");

        // Written by process (audio thread), read by the meter on the GUI thread.
        std::atomic<float> reduction{1.f};
        std::atomic<float> keyLevel{0.f};

    private:
        float env = 0.f;
        float gainSmoothed = 1.f;
};
