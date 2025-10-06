#include "Node.h"
#include "NodeManager.h"
#include "BusManager.h"

Node::Node(uint16_t id) : id(id) {}

Node::~Node() {}

void* Node::getInput(inputNode& i) {
    BusManager* bm = BusManager::get();
    NodeManager* nm = NodeManager::get();
    switch(i.type) {
        case inputType::BusE:
            return &(bm->getBusE(i.index)->events);
            break;
        case inputType::BusW:
            return bm->getBusW(i.index)->buffer;
            break;
        case inputType::NodeE:
            return nm->getNode(i.id)->getOutput(i.index);
            break;
        case inputType::NodeW:
            return nm->getNode(i.id)->getOutput(i.index);
            break;
        default:
            return nullptr;
    }
}

void* Node::getOutput(uint8_t index) {
    return outputs[index];
}
