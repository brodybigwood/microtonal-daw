#include "eventduplicator.h"

EventDuplicatorNode::EventDuplicatorNode(uint16_t id, NodeManager* nm)
    : Node(id, nm, NodeType::EventDuplicator) {
    in = new Connection;
    in->type = DataType::Events;
    in->dir = Direction::input;
    inputs.addConnection(in);

    for (int i = 0; i < kOutputs; ++i) addOutput();
}

void EventDuplicatorNode::addOutput() {
    auto out = new Connection;
    out->type = DataType::Events;
    out->dir = Direction::output;
    outputs.addConnection(out);
}

void EventDuplicatorNode::process() {
    for (Connection* c : outputs.connections) {
        if (!c || c->type != DataType::Events || !c->events) continue;
        c->events->clear();
        if (in->is_connected && in->events)
            *c->events = *in->events;
    }
}
