#include "OutputNode.h"

OutputNode::OutputNode() : Node(0) {
    dstRect.x = 50.0f;
    dstRect.y = 50.0f;
}

void OutputNode::process() {
    for(int i = 0; i < numChannels; i++) {
        if(i > inputs.connections.size() - 1) return;
        Connection* c = inputs.connections[i];

        if(!c->is_connected) {
            std::memset(output + i*bufferSize, 0, bufferSize * sizeof(float));
        }

        void* data = c->data;
        auto bus = getWaveform(data);

        float* inputBuffer = bus->buffer;
        int& bufferSize = bus->bufferSize;

        std::memcpy(output + i*bufferSize, inputBuffer, bufferSize * sizeof(float));
    }
}

void OutputNode::setup() {
    for(int i = 0; i < 32; i++) {
        Connection* c = new Connection;
        c->id = inputs.id_pool.newID();
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.connections.push_back(c);
    }
}
