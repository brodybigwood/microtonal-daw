#include "gain.h"
#include <cstring>

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
        std::memset(out->buffer, 0, static_cast<size_t>(bufferSize) * sizeof(float));
        return;
    }

    for (int i = 0; i < bufferSize; ++i) {
        out->buffer[i] = in->buffer[i] * gain[i] * 2.0f;
    }
}
