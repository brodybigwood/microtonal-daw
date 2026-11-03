#include "duplicator.h"
#include <cstring>

DuplicatorNode::DuplicatorNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Duplicator) {
    for (int i = 0; i < NUM_DUPLICATE_OUTPUTS; ++i) addOutput();

    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    in->numChannels = 1;
    inputs.addConnection(in);
}

void DuplicatorNode::process() {
    if (!in->is_connected) {
        for (Connection* c : outputs.connections) {
            if (c->buffer)
                std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) *
                    static_cast<size_t>(c->numChannels) * sizeof(float));
        }
        return;
    }

    const int inCh = in->numChannels;
    for (Connection* c : outputs.connections) {
        if (!c->is_connected || !c->buffer) continue;
        const int outCh = c->numChannels;
        for (int ch = 0; ch < outCh; ++ch) {
            if (ch < inCh) {
                std::memcpy(c->channel(ch), in->channel(ch),
                            static_cast<size_t>(bufferSize) * sizeof(float));
            } else {
                std::memset(c->channel(ch), 0, static_cast<size_t>(bufferSize) * sizeof(float));
            }
        }
    }
}

void DuplicatorNode::addOutput() {
    auto out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);
}

void DuplicatorNode::setup() {
}
