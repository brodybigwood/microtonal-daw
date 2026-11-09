#include "paninvert.h"

PanInvertNode::PanInvertNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::PanInvert) {
    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    in->minChannels = 2;
    in->updateNumChannels();
    inputs.addConnection(in);

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    out->minChannels = 2;
    out->updateNumChannels();
    outputs.addConnection(out);
}

void PanInvertNode::process() {
    if (!out->buffer) return;
    const int bs = bufferSize;
    const int outCh = std::max(1, out->numChannels);

    for (int c = 0; c < outCh; ++c) {
        if (float* buf = out->channel(c))
            std::memset(buf, 0, static_cast<size_t>(bs) * sizeof(float));
    }
    if (!in || !in->is_connected || !in->buffer) return;

    const int inCh = std::max(1, in->numChannels);
    const float* inL = in->channel(0);
    const float* inR = inCh > 1 ? in->channel(1) : inL;
    float* outL = out->channel(0);
    float* outR = outCh > 1 ? out->channel(1) : nullptr;
    for (int i = 0; i < bs; ++i) {
        if (outL) outL[i] = inR ? inR[i] : 0.0f;
        if (outR) outR[i] = inL ? inL[i] : 0.0f;
    }
}
