#include "NodeManager.h"
#include "Node.h"
#include "BusManager.h"
#include "NodeEditor.h"

#include <iostream>
#include "nodes/nodetypes.h"

NodeManager::NodeManager(Project* p) : project(p) {
    ne = new NodeEditor;
    ne->nm = this;
    outNode = new OutputNode(this);
    id_pool.reserveID(0); // id of outputnode
}

json NodeManager::serialize() {
    json j;
    j["idManager"] = id_pool.toJSON();

    j["nodes"] = json::array();
    j["connections"] = json::array();

    j["outNode"] = outNode->serialize();

    auto serializeConnections = [&j] (Node* n) {
        for (auto c : n->inputs.connections) {
            if (!c->is_connected) continue;
            json s;
            auto src = static_cast<sourceNode*>(c->data);

            s["dataType"] = c->type;
            s["connectionType"] = src->type;

            s["srcNodeID"] = src->source_id;
            s["srcConID"] = src->output_id;
            s["dstNodeID"] = n->id;
            s["dstConID"] = c->id;

            j["connections"].push_back(s);
        }
    };

    serializeConnections(outNode);
    for (auto n : nodes) {
        serializeConnections(n);
        j["nodes"].push_back(n->serialize());
    }

    return j;
}

void NodeManager::deSerialize(json j) {
    id_pool.fromJSON(j["idManager"]);

    for (auto n : j["nodes"]) {
        auto node = Node::deSerialize(n, this);
        if (node) {
            nodes.push_back(node); 
            ids[node->id] = nodes.size() - 1;
        }
    }
    
    outNode->deSerialize(j["outNode"]);

    for (auto s : j["connections"]) {
        Node* dstNode;
        int dstNodeID = s["dstNodeID"];
        if (dstNodeID) dstNode = getNode(dstNodeID);
        else dstNode = outNode;
        
        auto dstConID = s["dstConID"];
        switch (s["connectionType"].get<int>()) {
            case ConnectionType::bus: {
                    Bus* srcBus;
                    int busID = s["srcNodeID"];
                    auto bm = BusManager::get();
                    switch (s["dataType"].get<int>()) {
                        case DataType::Events:
                            srcBus = bm->getBusE(busID);
                            break;
                        case DataType::Waveform:
                            srcBus = bm->getBusW(busID);
                            break;
                    }
                    makeBusConnection(srcBus, dstNode, dstConID);
                }
                break;
            case ConnectionType::node: {
                    auto srcNode = getNode(s["srcNodeID"]);
                    auto srcConID = s["srcConID"];
                    makeNodeConnection(srcNode, srcConID, dstNode, dstConID);
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

NodeManager::~NodeManager() {
    for(auto n : nodes) {
        delete n;
    }

    nodes.clear();
}

std::vector<Node*>& NodeManager::getNodes() { return nodes; };
void NodeManager::makeNodeConnection(
        Node* srcNode, uint16_t srcConID,
        Node* dstNode, uint16_t dstConID
) {
    if (srcNode->depends(dstNode)) return; // prevent circular dependency

    Connection* srcCon = srcNode->outputs.getConnection(srcConID); 
    Connection* dstCon = dstNode->inputs.getConnection(dstConID);
    
    if(srcCon->type != dstCon->type || srcCon->is_connected || dstCon->is_connected) return;

    sourceNode* s = new sourceNode;
    s->type = node;
    s->source_id = srcNode->id;
    s->output_id = srcConID;

    dstCon->data = s;

    srcCon->output_node = dstNode->id;
    srcCon->output_connection = dstConID;

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
            else dstNode = outNode;
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
        case NodeType::Arranger:
            n = new ArrangerNode(id, this);
            break;
        case NodeType::Oscillator:
            n = new OscillatorNode(id, this);
            break;
        case NodeType::Merger:
            n = new MergerNode(id, this);
            break;
        case NodeType::Splitter:
            n = new SplitterNode(id, this);
            break;
        case NodeType::Delay:
            n = new DelayNode(id, this);
            break;
        case NodeType::Panner:
            n = new PannerNode(id, this);
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
        outNode->numChannels = numChannels;
        outNode->update(bufferSize, sampleRate);
    }
   
    outNode->output = output;
    outNode->processTree();
    outNode->resetProcessTree();
}
