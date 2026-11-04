#include "gain.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// Knob 0..1 -> linear gain. Centre stays unity and the lower half keeps the old
// linear taper (0 at 0, 0.5 at 0.25) so existing projects are unchanged; the
// upper half is a dB curve reaching +48 dB (x251.19) at full.
float gainFactor(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    if (v <= 0.0f) return 0.0f;
    if (v <= 0.5f) return v * 2.0f;
    return std::exp2((v - 0.5f) * (96.0f / 6.020599913f));
}
} // namespace

GainNode::GainNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Gain) {
    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);

    params.push_back(&gain);
}

void GainNode::setup() {}

void GainNode::process() {
    if (!out || !out->buffer) return;

    if (!in || !in->is_connected || !in->buffer) {
        std::memset(out->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(out->numChannels) * sizeof(float));
        return;
    }

    const int inCh = std::max(1, in->numChannels);
    for (int ch = 0; ch < out->numChannels; ++ch) {
        float* outCh = out->channel(ch);
        if (!outCh) continue;
        // The output can be wider than the input when something downstream requires
        // more channels than the source carries; the extra channels are silent.
        if (ch >= inCh) {
            std::memset(outCh, 0, static_cast<size_t>(bufferSize) * sizeof(float));
            continue;
        }
        float* srcCh = in->channel(ch);
        for (int i = 0; i < bufferSize; ++i) {
            outCh[i] = srcCh[i] * gainFactor(gain[i]);
        }
    }
}
