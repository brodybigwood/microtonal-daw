#include "delay.h"

void DelayNode::process() {

}

DelayNode::DelayNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Delay) {
    Connection* input = new Connection;
    input->type = DataType::Waveform;
    inputs.addConnection(input);

    Connection* output = new Connection;
    output->type = DataType::Waveform;
    outputs.addConnection(output);
}

