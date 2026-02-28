#include "panner.h"
#include <cmath>

PannerNode::PannerNode(uint16_t id) : Node(id, NodeType::Panner) {

    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);

    l = new Connection;
    l->type = DataType::Waveform;
    l->dir = Direction::output;
    outputs.addConnection(l);

    r = new Connection;
    r->type = DataType::Waveform;
    r->dir = Direction::output;
    outputs.addConnection(r);
}

void PannerNode::process() {
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

    float pan = 0.75;
    
    auto getBufferOut = [this] (Connection* c) {
        void* data = getOutput(c);
        auto outBus = getWaveform(data);
        return outBus->buffer;
    };

    auto outBufferL = getBufferOut(l);
    auto outBufferR = getBufferOut(r);

    for (size_t i = 0; i < bufferSize; ++i) {
        auto angle = pan * M_PI_2; // will use a buffer for per-sample pan in the future
        outBufferL[i] = inBuffer[i] * cosf(angle);
        outBufferR[i] = inBuffer[i] * sinf(angle);
    }
}

void PannerNode::setup() {
}
