#pragma once

#include "Node.h"
#include <atomic>
#include <unordered_map>

// Sidechain ducker: audio in + key in, one audio out. The key's level drives a
// smooth gain reduction on the audio path (classic kick-triggered pumping).
// No plugin involved, so no editor and no channel/format surprises.
class DuckerNode : public Node {
    public:
        DuckerNode(uint16_t, NodeManager*);

        Connection* in = nullptr;   // audio to duck
        Connection* key = nullptr;  // sidechain trigger (audio)
        Connection* events = nullptr; // optional note trigger: note-on adds 1.0 to the key
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

        // Amplitude-vs-time display: input level and the duck curve (linear
        // gain, 1 = 0 dB). Copied to the GUI copy by the audio thread via
        // enqueueProcessorAction, the same way VisualizerNode does it.
        std::vector<float> guiLevel;
        std::vector<float> guiGain;
        size_t guiPos = 0;

    private:
        static constexpr size_t kHist = 120;
        float env = 0.f;
        float gainSmoothed = 1.f;
        float dispLevel = 0.f; // display-only input envelope
        float eventGate = 0.f; // 1 while a note-on is held on the event input
        std::unordered_map<int, float> heldNotes; // event-input notes currently down
        std::vector<float> levelHistory;
        std::vector<float> gainHistory;
        size_t histPos = 0;
        void syncToGui();
};
