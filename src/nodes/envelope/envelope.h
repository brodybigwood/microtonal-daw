#pragma once

#include "Node.h"
#include <unordered_map>

class EnvelopeNode : public Node {
public:
    EnvelopeNode(uint16_t, NodeManager*);

    void process() override;
    void setup() override;
    json extraSerialize() override;
    void extraDeSerialize(const json&) override;

private:
    enum class Stage {
        Attack,
        Decay,
        Sustain,
        Release
    };

    struct VoiceState {
        Stage stage = Stage::Attack;
        float level = 0.0f;
        float releaseStep = 0.0f;
    };

    Connection* eventIn = nullptr;
    Connection* eventOut = nullptr;
    Connection* envelopeOut = nullptr;

    Knob attack = Knob(0.28f, NODE_W * 0.35f, NODE_H * 0.31f, 12.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Attack");
    Knob decay = Knob(0.22f, NODE_W * 0.65f, NODE_H * 0.31f, 12.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Decay");
    Knob sustain = Knob(0.60f, NODE_W * 0.35f, NODE_H * 0.69f, 12.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Sustain");
    Knob release = Knob(0.28f, NODE_W * 0.65f, NODE_H * 0.69f, 12.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Release");

    std::unordered_map<int, VoiceState> voices;

    static float mapTimeSeconds(float normalized);
};
