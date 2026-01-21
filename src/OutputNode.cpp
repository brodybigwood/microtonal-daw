#include "OutputNode.h"

OutputNode::OutputNode() : Node(0) {
    dstRect.x = 50.0f;
    dstRect.y = 50.0f;

    for(int i = 0; i < 4; i++) {
        Connection* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.addConnection(c);
    }
}

void OutputNode::process() {
    for(int i = 0; i < numChannels; i++) {
        return;
        if(i > inputs.connections.size() - 1) return;
        Connection* c = inputs.connections[i];

        if(!c->is_connected) {
            std::memset(output + i*bufferSize, 0, bufferSize * sizeof(float));
            return;
        }

        void* data = getInput(c);

        auto bus = getWaveform(data);

        float* inputBuffer = bus->buffer;
        int& bufferSize = bus->bufferSize;

        std::memcpy(output + i*bufferSize, inputBuffer, bufferSize * sizeof(float));
    }
}

void OutputNode::setup() {
}
