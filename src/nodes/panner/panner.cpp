#include "panner.h"
#include <cmath>

PannerNode::PannerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Panner) {
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

    params.push_back(&pan);
}

void PannerNode::process() {
    if (!in->is_connected) {
        for (Connection* c : outputs.connections) {
            float* outBuffer = c->buffer;
            if (!outBuffer) continue;
            std::memset(outBuffer, 0, bufferSize * sizeof(float));
        }
        return;
    }
    float* inBuffer = in->buffer;
    if (!inBuffer) {
        for (Connection* c : outputs.connections) {
            float* outBuffer = c->buffer;
            if (!outBuffer) continue;
            std::memset(outBuffer, 0, bufferSize * sizeof(float));
        }
        return;
    }

    auto getBufferOut = [this] (Connection* c) {
        return c->buffer;
    };

    auto outBufferL = getBufferOut(l);
    auto outBufferR = getBufferOut(r);
    if (!outBufferL || !outBufferR) return;

    for (size_t i = 0; i < bufferSize; ++i) {
        auto angle = pan[i] * M_PI_2;
        outBufferL[i] = inBuffer[i] * cosf(angle);
        outBufferR[i] = inBuffer[i] * sinf(angle);
    }
}

void PannerNode::setup() {
}
