#include "Node.h"
#include "NodeManager.h"
#include "BusManager.h"

Node::Node(uint16_t id) : id(id) {}

Node::~Node() {}

void* Node::getInput(uint16_t id) {
    Connection* con = inputs.getConnection(id);
    return con->data;
}

void* Node::getOutput(uint16_t id) {
    Connection* con = outputs.getConnection(id);
    void* data = con->data;
    sourceNode* src = static_cast<sourceNode*>(data);

    switch(src->type) {
        case ConnectionType::bus:
        {
            auto bm = BusManager::get();
            Bus* b;
            switch(con->type) {
                case DataType::Events:
                {                            
                    b = bm->getBusE(src->source_id);
                }
                case DataType::Waveform:
                {
                    b = bm->getBusW(src->source_id);
                }
            }
            Connection* c = &(b->output);
            return c->data;
        }
        case ConnectionType::node:
        {
            auto nm = NodeManager::get();
            auto n = nm->getNode(src->source_id);
            return n->getOutput(src->output_id);
        }
    }
}

EventBus* getEvents(void* data){
    return static_cast<EventBus*>(data);
}

WaveformBus* getWaveform(void* data){
    return static_cast<WaveformBus*>(data);
}

uint16_t connectionSet::getIndex(uint16_t id) {
    return ids[id];
}

Connection* connectionSet::getConnection(uint16_t id) {
    auto index = getIndex(id);
    auto con = connections[index];
    return con;
}
