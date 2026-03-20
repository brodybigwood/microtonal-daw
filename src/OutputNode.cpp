#include "OutputNode.h"
#include <iostream>
OutputNode::OutputNode(NodeManager* nm) : Node(0, nm, NodeType::Count) {
    for(int i = 0; i < 4; i++) {
        Connection* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.addConnection(c);
    }

    move(50, 50);
}

void OutputNode::process() {
    for(int i = 0; i < numChannels; i++) {
        if(i > inputs.connections.size() - 1) return;
        Connection* c = inputs.connections[i];
        
        if(!c->is_connected) {
            std::memset(output + i*bufferSize, 0, bufferSize * sizeof(float));
            continue;
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

json OutputNode::serialize() {
    json j;

    j["x"] = dstRect.x;
    j["y"] = dstRect.y;
    j["zoomRatio"] = zoomRatio;

    return j;
}

void OutputNode::deSerialize(json j) {
    zoom(j["zoomRatio"].get<float>()/zoomRatio);
    move(j["x"], j["y"]);
}

