#include "NodeManager.h"
#include "Node.h"
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

            s["dataType"] = c->type;

            s["srcNodeID"] = c->input_node;
            s["srcConID"] = c->input_connection;
            s["dstNodeID"] = c->output_node;
            s["dstConID"] = c->output_connection;

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
        auto srcNode = getNode(s["srcNodeID"]);
        auto srcConID = s["srcConID"];
        makeNodeConnection(srcNode, srcConID, dstNode, dstConID);
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

    dstCon->buffer = srcCon->buffer;
    dstCon->events = srcCon->events;

    
    dstCon->input_node = srcNode->id;
    dstCon->input_connection = srcConID;

    srcCon->output_node = dstNode->id;
    srcCon->output_connection = dstConID;

    srcCon->is_connected = true;
    dstCon->is_connected = true;
}

void NodeManager::severConnection(Connection* c) {
    if (!c->is_connected) return;

    Connection* dstCon;
    Connection* srcCon;

    switch (c->dir) {
        case Direction::input: {
            dstCon = c;
            srcCon = getNode(dstCon->input_node)->outputs.getConnection(dstCon->input_connection);
            break;
        }
        case Direction::output: {
            srcCon = c;
            Node* dstNode;
            if (srcCon->output_node) dstNode = getNode(srcCon->output_node);
            else dstNode = outNode;
            dstCon = dstNode->inputs.getConnection(srcCon->output_connection);
            break;
        }
    }
    

    dstCon->input_node = -1;
    dstCon->input_connection = -1;
    srcCon->output_node = -1;
    srcCon->output_connection = -1;

    srcCon->is_connected = false;
    dstCon->is_connected = false;

    dstCon->buffer = nullptr;
    dstCon->events = nullptr;
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
