#include "NodeManager.h"
#include "Node.h"
#include "BusManager.h"

#include <iostream>
#include "nodes/nodetypes.h"

NodeManager::NodeManager() {
    id_pool.reserveID(0); // id of outputnode
}

json NodeManager::serialize() {
    json j;
    j["idManager"] = id_pool.toJSON();

    j["nodes"] = json::array();
    j["connections"] = json::array();

    j["outNode"] = outNode.serialize();

    for (auto c : outNode.inputs.connections) if (c->is_connected) {
        json s;
        auto src = static_cast<sourceNode*>(c->data);
        json ss;

        switch (src->type) {
            case ConnectionType::bus:
                ss = -1;
                break;
            case ConnectionType::node:
                ss = getNode(src->source_id)->id;
                break;
        }

        s["dataType"] = c->type;
        s["connectionType"] = src->type;

        s["source_id"] = src->source_id;
        s["source_node"] = ss;
        s["output_id"] = src->output_id;
        s["output_node"] = outNode.id;

        j["connections"].push_back(s);
    }

    for (auto n : nodes) {
        j["nodes"].push_back(n->serialize());
        for (auto c : n->inputs.connections) if (c->is_connected) {
            json s;
            auto src = static_cast<sourceNode*>(c->data);
            json ss;

            switch (src->type) {
                case ConnectionType::bus:
                    ss = -1;
                    break;
                case ConnectionType::node:
                    ss = getNode(src->source_id)->id;
                    break;
            }

            s["dataType"] = c->type;
            s["connectionType"] = src->type;

            s["source_id"] = src->source_id;
            s["source_node"] = ss;
            s["output_id"] = src->output_id;
            s["output_node"] = n->id;

            j["connections"].push_back(s);
        }
    }

    return j;
}

void NodeManager::deSerialize(json j) {
    id_pool.fromJSON(j["idManager"]);

    for (auto n : j["nodes"]) {
        auto node = Node::deSerialize(n);
        if (node) {
            nodes.push_back(node); 
            ids[node->id] = nodes.size() - 1;
        }
    }

    for (auto s : j["connections"]) {
        Node* dstNode;
        int i = s["output_node"];
        if (i) dstNode = getNode(i);
        else dstNode = &outNode;
        
        auto dstNodeID = s["output_id"];
        switch (s["connectionType"].get<int>()) {
            case ConnectionType::bus: {
                    Bus* source;
                    int source_id = s["source_id"];
                    auto bm = BusManager::get();
                    switch (s["dataType"].get<int>()) {
                        case DataType::Events:
                            source = bm->getBusE(source_id);
                            break;
                        case DataType::Waveform:
                            source = bm->getBusW(source_id);
                            break;
                    }
                    makeBusConnection(source, dstNode, dstNodeID);
                }
                break;
            case ConnectionType::node: {
                    auto srcNode = getNode(s["source_node"]);
                    auto srcNodeID = s["source_id"];
                    makeNodeConnection(srcNode, srcNodeID, dstNode, dstNodeID);
                }
                break;
        }
    }
}

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
    if (source->depends(dst)) return; // prevent circular dependency

    Connection* srcCon = source->outputs.getConnection(inputID); 
    Connection* dstCon = dst->inputs.getConnection(inputID);
    
    if(srcCon->type != dstCon->type || srcCon->is_connected || dstCon->is_connected) return;

    sourceNode* s = new sourceNode;
    s->type = node;
    s->source_id = source->id;
    s->output_id = srcCon->id;

    dstCon->data = s;

    srcCon->output_node = dst->id;
    srcCon->output_connection = dstCon->id;

    srcCon->is_connected = true;
    dstCon->is_connected = true;
}

void NodeManager::makeBusConnection(Bus* source, Node* dst, uint16_t inputID) {
    Connection* dstCon = dst->inputs.getConnection(inputID);
    Connection* srcCon = &(source->output);

    if (srcCon->type != dstCon->type || srcCon->is_connected || dstCon->is_connected) return;

    sourceNode* s = new sourceNode;
    s->type = bus;
    s->source_id = source->id;
    s->output_id = srcCon->id;
    
    dstCon->data = s;
    
    srcCon->output_node = dst->id;
    srcCon->output_connection = dstCon->id;

    srcCon->is_connected = true;
    dstCon->is_connected = true;

    std::cout << "connected bus " << source->id << " to node." << std::endl;
}

void NodeManager::severConnection(Connection* c) {
    if (!c->is_connected) return;

    Connection* dstCon;
    Connection* srcCon;

    switch (c->dir) {
        case Direction::input: {
            dstCon = c;
            auto s = static_cast<sourceNode*>(c->data);
            switch (s->type) {
                case ConnectionType::bus: {
                    auto bm = BusManager::get();
                    switch (c->type) {
                        case DataType::Events:
                            srcCon = &(bm->getBusE(s->source_id)->output);
                            break;
                        case DataType::Waveform:
                            srcCon = &(bm->getBusW(s->source_id)->output);
                            break;
                    }
                    break;
                }
                case ConnectionType::node:
                    srcCon = getNode(s->source_id)->outputs.getConnection(s->output_id);
                    break;
            }
            delete s;
            c->data = nullptr;
            break;
        }
        case Direction::output: {
            srcCon = c;
            Node* dstNode;
            if (srcCon->output_node) dstNode = getNode(srcCon->output_node);
            else dstNode = &outNode;
            dstCon = dstNode->inputs.getConnection(srcCon->output_connection);
            delete static_cast<sourceNode*>(dstCon->data);
            dstCon->data = nullptr;
            break;
        }
    }
    
    srcCon->output_node = -1;
    srcCon->output_connection = -1;

    srcCon->is_connected = false;
    dstCon->is_connected = false;
}

Node* NodeManager::addNode(NodeType t) {

    uint16_t id = id_pool.newID();
    Node* n = nullptr;

    switch (t) {
        case NodeType::Oscillator:
            n = new OscillatorNode(id);
            break;
        case NodeType::Merger:
            n = new MergerNode(id);
            break;
        case NodeType::Splitter:
            n = new SplitterNode(id);
            break;
        case NodeType::Delay:
            n = new DelayNode(id);
            break;
        case NodeType::Panner:
            n = new PannerNode(id);
            break;
        default:
            break;
    }
    
    nodes.push_back(n);
    ids[id] = nodes.size() - 1;

    n->update(bufferSize, sampleRate);
    return n;
}

void NodeManager::removeNode(Node* n) {

    for (auto c : n->inputs.connections) severConnection(c);
    for (auto c : n->outputs.connections) severConnection(c);

    id_pool.releaseID(n->id);
    auto index = ids[n->id];

    if (index != nodes.size() - 1) {
        std::swap(nodes[index], nodes.back());
        ids[nodes[index]->id] = index;
    }

    ids.erase(n->id);

    delete nodes.back();
    nodes.pop_back();
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
   
    outNode.output = output;
    outNode.processTree();
    outNode.resetProcessTree();
}
