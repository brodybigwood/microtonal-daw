#include "merger.h"
#include <iostream>
MergerNode::MergerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Merger) {

    for (int i = 0; i < NUM_INPUTS; i++) addInput();

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    out->numChannels = NUM_INPUTS;
    outputs.addConnection(out);
}

void MergerNode::process() {
    if (!out->is_connected || !out->buffer) return;

    std::memset(out->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(NUM_INPUTS) * sizeof(float));

    int chIdx = 0;
    for (Connection* c : inputs.connections) {
        if (!c->is_connected || c->type != DataType::Waveform) continue;
        if (chIdx >= NUM_INPUTS) break;
        float* inBuf = c->buffer;
        float* outCh = out->channel(chIdx);
        for (int i = 0; i < bufferSize; ++i)
            outCh[i] += inBuf[i];
        ++chIdx;
    }
}

void MergerNode::addInput() {
    auto in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);
}

void MergerNode::setup() {}
