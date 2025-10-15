#include "NodeManager.h"
#include "Node.h"

#include "nodes/nodetypes.h"


Node* NodeManager::getNode(uint16_t id) {
    auto it = ids.find(id);
    if (it == ids.end()) return nullptr;
    return nodes[it->second];
}

NodeManager* NodeManager::get() {
    static NodeManager nm;
    return &nm;
}

NodeManager::~NodeManager() {
    for(auto n : nodes) {
        delete n;
    }

    nodes.clear();
}

std::vector<Node*>& NodeManager::getNodes() { return nodes; };
void NodeManager::makeNodeConnection(
        Node* source, uint16_t outputID,
        Node* dst, uint16_t inputID
) {
    Connection* srcCon = source->outputs.getConnection(inputID); 
    Connection* dstCon = dst->inputs.getConnection(inputID);
    
    if(srcCon->type != dstCon->type || srcCon->is_connected || dstCon->is_connected) return;

    sourceNode* s = new sourceNode;
    s->type = node;
    s->source_id = source->id;
    s->output_id = srcCon->id;

    dstCon->data = s;

    srcCon->is_connected = true;
    dstCon->is_connected = true;
}

void NodeManager::makeBusConnection(Bus* source, Node* dst, uint16_t inputID) {
    Connection* con = dst->inputs.getConnection(inputID);

}

Node* NodeManager::addNode() {
    uint16_t id = id_pool.newID();
    Node* n = new OscillatorNode(id);
    nodes.push_back(n);
    ids[id] = nodes.size() - 1;

    n->update(bufferSize, sampleRate);
    return n;
}

void NodeManager::removeNode(uint16_t id) {
    auto it = ids.find(id);
    if (it == ids.end()) return;

    uint16_t index = it->second;

    id_pool.releaseID(id);

    if (index != nodes.size() - 1) {
        std::swap(nodes[index], nodes.back());
        ids[nodes[index]->id] = index;
    }

    delete nodes.back();
    nodes.pop_back();
    ids.erase(id);
}

void NodeManager::process(float* output, int& bufferSize, int& numChannels, int& sampleRate) {

    bool update = false;
    if(bufferSize != this->bufferSize) {
        update = true;
        this->bufferSize = bufferSize;
    }
    if(numChannels != this->numChannels) {
        update = true;
        this->numChannels = numChannels;
    }
    if(sampleRate != this->sampleRate) {
        update = true;
        this->sampleRate = sampleRate;
    }

    if(update) {
        for(auto node : nodes) {
            node->update(bufferSize, sampleRate);
        }
        outNode.numChannels = numChannels;
        outNode.update(bufferSize, sampleRate);
    }
   
    for(auto node : nodes) {
        node->process();
    }

    outNode.output = output;
    outNode.process();
}
