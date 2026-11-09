#pragma once

#include "Node.h"

// Duplicates an event (MIDI/note) stream: one Events input, every Events
// output carries a full copy of the incoming event list.
class EventDuplicatorNode : public Node {
public:
    EventDuplicatorNode(uint16_t, NodeManager*);

    void process() override;
    void setup() override {}

private:
    static constexpr int kOutputs = 8;
    Connection* in = nullptr;
    void addOutput();
};
