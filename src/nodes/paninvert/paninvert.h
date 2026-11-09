#pragma once

#include "Node.h"

/// Swaps left/right: output L = input R, output R = input L. Inverts the stereo
/// image; used as the cross-feedback element of a ping-pong delay.
class PanInvertNode : public Node {
public:
    PanInvertNode(uint16_t, NodeManager*);

    void process() override;
    bool widthFollowsInput() const override { return true; }

private:
    Connection* in = nullptr;
    Connection* out = nullptr;
};
