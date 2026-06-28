#include "splitter.h"

SplitterNode::SplitterNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Splitter) {

    for (int i = 0; i < NUM_OUTPUTS; i++) addOutput();

    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    in->numChannels = NUM_OUTPUTS;
    inputs.addConnection(in);
}

void SplitterNode::process() {
    if (!in->is_connected) {
        for (Connection* c : outputs.connections) {
            std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(c->numChannels) * sizeof(float));
        }
        return;
    }

    int maxCh = in->numChannels;
    int chIdx = 0;
    for (Connection* c : outputs.connections) {
        if (c->is_connected) {
            for (int ch = 0; ch < c->numChannels; ++ch) {
                if (chIdx < maxCh) {
                    std::memcpy(c->channel(ch), in->channel(chIdx), static_cast<size_t>(bufferSize) * sizeof(float));
                } else {
                    std::memset(c->channel(ch), 0, static_cast<size_t>(bufferSize) * sizeof(float));
                }
                ++chIdx;
            }
        } else {
            chIdx += c->numChannels;
        }
    }
}

void SplitterNode::addOutput() {
    auto out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);
}

void SplitterNode::setup() {
}
