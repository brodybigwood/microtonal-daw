#include "panner.h"
#include <cmath>

PannerNode::PannerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Panner) {
    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    out->minChannels = 2;
    out->updateNumChannels();
    outputs.addConnection(out);

    params.push_back(&pan);
}

void PannerNode::process() {
    if (!in->is_connected) {
        for (Connection* c : outputs.connections) {
            for (int ch = 0; ch < c->numChannels; ++ch) {
                float* buf = c->channel(ch);
                if (!buf) continue;
                std::memset(buf, 0, bufferSize * sizeof(float));
            }
        }
        return;
    }

    if (out->numChannels < 2) return;

    float* inBuf = in->buffer;
    float* outL = out->channel(0);
    float* outR = out->channel(1);

    for (size_t i = 0; i < bufferSize; ++i) {
        auto angle = pan[i] * M_PI_2;
        outL[i] = inBuf[i] * cosf(angle);
        outR[i] = inBuf[i] * sinf(angle);
    }
}

void PannerNode::setup() {
}
