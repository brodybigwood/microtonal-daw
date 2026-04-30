#include "NodeManager.h"
#include "Node.h"
#include "NodeEditor.h"
#include "UndoManager.h"
#include "Project.h"

#include <iostream>
#include "nodes/nodetypes.h"

NodeManager::NodeManager(Project* p, std::vector<int> managerPath) : project(p), managerPath(std::move(managerPath)) {
    outNode = new OutputNode(this);
    id_pool.reserveID(0); // id of outputnode
}

void NodeManager::setNE(NodeEditor* ne) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    this->ne = ne;
    ne->nm = this;
    outNode->setNE(ne);
    for (auto n : nodes) n->setNE(ne);
}

void NodeManager::resetNE() {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    if (ne) ne->nm = nullptr;
    ne = nullptr;
    outNode->resetNE();
    for (auto n : nodes) n->resetNE();
}

json NodeManager::serialize() {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
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
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
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
        makeNodeConnectionNow(srcNode->id, srcConID, dstNode->id, dstConID);
    }
}

Node* NodeManager::getNode(uint16_t id) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    auto it = ids.find(id);
    if (it == ids.end()) return nullptr;
    return nodes[it->second];
}

NodeManager::~NodeManager() {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    flushUiDeferred();
    for(auto n : nodes) {
        delete n;
    }

    nodes.clear();
}

std::vector<Node*> NodeManager::getNodes() {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    return nodes;
};
void NodeManager::makeNodeConnection(
        Node* srcNode, uint16_t srcConID,
        Node* dstNode, uint16_t dstConID
) {
    auto pa = new MakeNodeConnectionAction(project, managerPath, srcNode->id, srcConID, dstNode->id, dstConID);
    project->um->newAction(pa);
}

void NodeManager::severConnection(Connection* c) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    if (!c->is_connected) return;
    uint16_t srcNodeID, srcConID, dstNodeID, dstConID;
    if (c->dir == Direction::input) {
        dstNodeID = c->output_node;
        dstConID = c->output_connection;
        srcNodeID = c->input_node;
        srcConID = c->input_connection;
    } else {
        srcNodeID = 0;
        srcConID = c->id;
        for (auto n : nodes) {
            for (auto oc : n->outputs.connections) {
                if (oc == c) {
                    srcNodeID = n->id;
                    break;
                }
            }
            if (srcNodeID) break;
        }
        if (!srcNodeID) return;
        dstNodeID = c->output_node;
        dstConID = c->output_connection;
    }
    auto pa = new SeverNodeConnectionAction(project, managerPath, srcNodeID, srcConID, dstNodeID, dstConID);
    project->um->newAction(pa);
}

void NodeManager::addNode(NodeType t, float x, float y) {
    auto pa = new AddNodeAction(project, managerPath, t, x, y);
    project->um->newAction(pa);
}

void NodeManager::removeNode(Node* n) {
    auto pa = new RemoveNodeAction(project, managerPath, n->id);
    project->um->newAction(pa);
}

void NodeManager::process(float* output, int& bufferSize, int& numChannels, int& sampleRate) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
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

    // Apply deferred graph edits at the audio-buffer boundary.
    project->um->flushAudioActions();
}

Node* NodeManager::addNodeNow(NodeType t, float x, float y, int forcedID) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    uint16_t id;
    if (forcedID >= 0) {
        id = static_cast<uint16_t>(forcedID);
        id_pool.reserveID(id);
    } else {
        id = id_pool.newID();
    }

    Node* n = byType(t, id, this);
    if (!n) return nullptr;

    n->move(x, y);
    nodes.push_back(n);
    ids[id] = nodes.size() - 1;

    n->update(bufferSize, sampleRate);
    if (ne) {
        n->ne = ne;
        n->mouseX = &ne->mouseX;
        n->mouseY = &ne->mouseY;
        n->window = ne->window;
        n->renderer = ne->renderer;
    }
    return n;
}

Node* NodeManager::addNodeNow(json j) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    uint16_t id = j["id"];
    id_pool.reserveID(id);
    auto node = Node::deSerialize(j, this);
    if (!node) return nullptr;
    nodes.push_back(node);
    ids[id] = nodes.size() - 1;
    node->update(bufferSize, sampleRate);
    if (ne) {
        node->ne = ne;
        node->mouseX = &ne->mouseX;
        node->mouseY = &ne->mouseY;
        node->window = ne->window;
        node->renderer = ne->renderer;
    }
    return node;
}

void NodeManager::removeNodeNow(uint16_t id) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    auto node = getNode(id);
    if (!node) return;

    for (auto c : node->inputs.connections) {
        if (c->is_connected) {
            severConnectionNow(c->input_node, c->input_connection, node->id, c->id);
        }
    }
    for (auto c : node->outputs.connections) {
        if (c->is_connected) {
            severConnectionNow(node->id, c->id, c->output_node, c->output_connection);
        }
    }

    id_pool.releaseID(node->id);
    auto index = ids[node->id];

    if (index != nodes.size() - 1) {
        std::swap(nodes[index], nodes.back());
        ids[nodes[index]->id] = index;
    }

    ids.erase(node->id);
    Node* removed = nodes.back();
    nodes.pop_back();
    {
        std::lock_guard<std::mutex> lock(deferredDeleteMutex);
        deferredDeleteNodes.push_back(removed);
    }
}

void NodeManager::makeNodeConnectionNow(uint16_t srcNodeID, uint16_t srcConID, uint16_t dstNodeID, uint16_t dstConID) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    auto srcNode = getNode(srcNodeID);
    Node* dstNode = dstNodeID ? getNode(dstNodeID) : outNode;
    if (!srcNode || !dstNode) return;
    if (srcNode->depends(dstNode)) return;

    Connection* srcCon = srcNode->outputs.getConnection(srcConID);
    Connection* dstCon = dstNode->inputs.getConnection(dstConID);
    if (!srcCon || !dstCon) return;
    if (srcCon->type != dstCon->type || srcCon->is_connected || dstCon->is_connected) return;

    dstCon->buffer = srcCon->buffer;
    dstCon->events = srcCon->events;
    dstCon->input_node = srcNode->id;
    dstCon->input_connection = srcConID;

    srcCon->output_node = dstNode->id;
    srcCon->output_connection = dstConID;
    srcCon->is_connected = true;
    dstCon->is_connected = true;
}

void NodeManager::severConnectionNow(uint16_t srcNodeID, uint16_t srcConID, uint16_t dstNodeID, uint16_t dstConID) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    auto srcNode = getNode(srcNodeID);
    Node* dstNode = dstNodeID ? getNode(dstNodeID) : outNode;
    if (!srcNode || !dstNode) return;

    auto srcCon = srcNode->outputs.getConnection(srcConID);
    auto dstCon = dstNode->inputs.getConnection(dstConID);
    if (!srcCon || !dstCon) return;
    if (!srcCon->is_connected || !dstCon->is_connected) return;

    dstCon->input_node = -1;
    dstCon->input_connection = -1;
    srcCon->output_node = -1;
    srcCon->output_connection = -1;
    srcCon->is_connected = false;
    dstCon->is_connected = false;
    dstCon->buffer = nullptr;
    dstCon->events = nullptr;
}

void NodeManager::moveNodeNow(uint16_t nodeID, float x, float y) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    Node* node = nullptr;
    if (nodeID == 0) {
        node = outNode;
    } else {
        node = getNode(nodeID);
    }
    if (!node) return;
    node->move(x, y);
}

bool NodeManager::snapshotNode(uint16_t nodeID, json& nodeData, json& connections) {
    std::lock_guard<std::recursive_mutex> lock(graphMutex);
    auto node = getNode(nodeID);
    if (!node) return false;

    nodeData = node->serialize();
    connections = json::array();

    for (auto c : node->inputs.connections) {
        if (!c->is_connected) continue;
        json s;
        s["srcNodeID"] = c->input_node;
        s["srcConID"] = c->input_connection;
        s["dstNodeID"] = nodeID;
        s["dstConID"] = c->id;
        connections.push_back(s);
    }

    for (auto c : node->outputs.connections) {
        if (!c->is_connected) continue;
        json s;
        s["srcNodeID"] = nodeID;
        s["srcConID"] = c->id;
        s["dstNodeID"] = c->output_node;
        s["dstConID"] = c->output_connection;
        connections.push_back(s);
    }

    return true;
}

void NodeManager::flushUiDeferred() {
    std::vector<Node*> pending;
    {
        std::lock_guard<std::mutex> lock(deferredDeleteMutex);
        pending.swap(deferredDeleteNodes);
    }
    for (auto* n : pending) {
        delete n;
    }
}
