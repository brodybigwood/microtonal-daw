#pragma once

#include "Node.h"

class GainNode : public Node {
public:
    GainNode(uint16_t, NodeManager*);

    void process() override;
    void setup() override;
    bool widthFollowsInput() const override { return true; }

private:
    Connection* in = nullptr;
    Connection* out = nullptr;

    Knob gain = Knob(0.5f, NODE_W * 0.5f, NODE_H * 0.5f, 16.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Gain");
};
