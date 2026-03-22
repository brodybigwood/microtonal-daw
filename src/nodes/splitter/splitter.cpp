#include "splitter.h"

SplitterNode::SplitterNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Splitter) {

    for (int i = 0; i < NUM_OUTPUTS; i++) addOutput();

    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);
}

void SplitterNode::process() {
    if (!in->is_connected) {
        for (Connection* c : outputs.connections) {
            float* outBuffer = c->buffer;
            std::memset(outBuffer, 0, bufferSize * sizeof(float));
        }
        return;
    }
    float* inBuffer = in->buffer;

    for (Connection* c : outputs.connections) {
        if (c->is_connected) {
            float* outBuffer = c->buffer;
            for (size_t i = 0; i < bufferSize; ++i) {
                outBuffer[i] = inBuffer[i];
            }
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
