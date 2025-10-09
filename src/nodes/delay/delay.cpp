#include "delay.h"

void DelayNode::process() {

}

DelayNode::DelayNode(uint16_t id) : Node(id) {
    Connection* input = new Connection;
    input->id = inputs.id_pool.newID();
    input->type = DataType::Waveform;
    inputs.connections.push_back(input);

    Connection* output = new Connection;
    output->id = outputs.id_pool.newID();
    output->type = DataType::Waveform;
    outputs.connections.push_back(output);
}

