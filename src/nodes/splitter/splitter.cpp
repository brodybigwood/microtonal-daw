#include "splitter.h"

SplitterNode::SplitterNode(uint16_t id) : Node(id) {

    for (int i = 0; i < NUM_OUTPUTS; i++) addOutput();

    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);
}

void SplitterNode::process() {
    if (!in->is_connected) {
        for (Connection* c : outputs.connections) {
            void* data = getOutput(c);
            auto outBus = getWaveform(data);
            float* outBuffer = outBus->buffer;
            std::memset(outBuffer, 0, bufferSize * sizeof(float));
        }
        return;
    }
    void* data = getInput(in);
    auto inBus = getWaveform(data);
    float* inBuffer = inBus->buffer;

    for (Connection* c : outputs.connections) {
        if (c->is_connected) {
            void* data = getOutput(c);
            auto outBus = getWaveform(data);
            float* outBuffer = outBus->buffer;
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
