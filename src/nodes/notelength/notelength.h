#pragma once

#include "Node.h"
#include <unordered_map>
#include <unordered_set>

// Clamps the length of incoming notes: every note-off is emitted early when the
// note has lasted longer than the Length parameter. The original (later)
// note-off is suppressed.
class NoteLengthNode : public Node {
public:
    NoteLengthNode(uint16_t, NodeManager*);

    void process() override;
    void setup() override;

private:
    struct PendingNote {
        int remainingSamples = 0;
        int pitch = 0;
        int channel = 0;
    };

    Connection* in = nullptr;
    Connection* out = nullptr;

    Knob length = Knob(0.5f, NODE_W * 0.5f, NODE_H * 0.55f, 14.0f, "assets/knobs/1.png",
                       -135.0f, 135.0f, "Length beats");

    std::unordered_map<int, PendingNote> pending;  // note id -> countdown
    std::unordered_set<int> closedEarly;           // ids whose off was already sent
};
