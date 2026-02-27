#include "merger.h"
#include <iostream>
MergerNode::MergerNode(uint16_t id) : Node(id, NodeType::Merger) {

    for (int i = 0; i < NUM_INPUTS; i++) addInput();

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);
}

void MergerNode::process() {
    if (!out->is_connected) return;
    auto bus = getWaveform(out->data);
    float* outBuffer = bus->buffer;
    std::memset(outBuffer, 0, bufferSize * sizeof(float));
    for (Connection*  c : inputs.connections) {
        if (!c->is_connected) continue;
        void* data = getInput(c);
        auto inBus = getWaveform(data);
        float* inBuffer = inBus->buffer;
        for (size_t i = 0; i < bufferSize; ++i) {
            outBuffer[i] += inBuffer[i];
        }
    }
}

void MergerNode::addInput() {
    auto in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);
}

void MergerNode::setup() {
}
