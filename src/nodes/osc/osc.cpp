#include "osc.h"
#include <cmath>

OscillatorNode::OscillatorNode(uint16_t id) : Node(id) {
    output = new Connection;
    output->type = DataType::Waveform;
    output->dir = Direction::output;
    outputs.addConnection(output);
}

void OscillatorNode::process() {
    if(!output->is_connected) return;
    auto bus = getWaveform(output->data);
    float* buffer = bus->buffer;

    for(int i = 0; i < bufferSize; i++) {
        buffer[i] = std::sin(i);
    }
}
