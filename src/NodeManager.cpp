#include "NodeManager.h"
#include "Node.h"
#include "NodeEditor.h"
#include "UndoManager.h"
#include "Project.h"
#include <SDL3/SDL.h>

#include <iostream>
#include <limits>
#include "nodes/nodetypes.h"

NodeManager::NodeManager(Project* p, std::vector<int> managerPath) : project(p), managerPath(std::move(managerPath)) {
    outNode = new OutputNode(this);
    inNode = new InputNode(this);
    id_pool.reserveID(0); // id of outputnode
    id_pool.reserveID(1); // id of inputnode
    topologyDirty = true;
}

void NodeManager::setNE(NodeEditor* ne) {
    this->ne = ne;
    ne->nm = this;
    ne->reserveEwID(0);
    ne->reserveEwID(1);
    outNode->setNE(ne);
    inNode->setNE(ne);
    int ww = ne->windowWidth;
    int wh = ne->windowHeight;
    ne->updateRootMenuBarLayout();
    outNode->placeDefaultByWindowSize(static_cast<float>(ww), static_cast<float>(wh));
    inNode->placeDefaultByWindowSize(static_cast<float>(ww), static_cast<float>(wh));
    for (auto n : nodes) n->setNE(ne);
    for (auto n : nodes) n->makeConnectionRects();
    inNode->makeConnectionRects();
    outNode->makeConnectionRects();
}

void NodeManager::resetNE() {
    if (ne) {
        ne->resetRootMenuBarLayout();
        ne->nm = nullptr;
    }
    ne = nullptr;
    outNode->resetNE();
    inNode->resetNE();
    for (auto n : nodes) n->resetNE();
}

json NodeManager::serialize() {
    json j;
    j["idManager"] = id_pool.toJSON();
    if (ne) j["ewIdPool"] = ne->ewIdPoolToJSON();

    j["nodes"] = json::array();
    j["connections"] = json::array();

    j["outNode"] = outNode->serialize();
    j["inNode"] = inNode->serialize();

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
    serializeConnections(inNode);
    for (auto n : nodes) {
        serializeConnections(n);
        j["nodes"].push_back(n->serialize());
    }

    return j;
}

void NodeManager::deSerialize(json j) {
    std::cout << "[DBG_DESER] NodeManager::deSerialize begin path=";
    for (size_t i = 0; i < managerPath.size(); ++i) {
        std::cout << managerPath[i] << (i + 1 < managerPath.size() ? "/" : "");
    }
    if (managerPath.empty()) std::cout << "root";
    std::cout << std::endl;
    id_pool.fromJSON(j["idManager"]);
    if (ne && j.contains("ewIdPool")) ne->ewIdPoolFromJSON(j["ewIdPool"]);

    for (auto n : j["nodes"]) {
        auto node = Node::deSerialize(n, this);
        if (node) {
            nodes.push_back(node);
            ids[node->id] = nodes.size() - 1;
            if (ne) node->setNE(ne);
            std::cout << "[DBG_DESER]  node restored id=" << node->id << " type=" << static_cast<int>(node->nodeType) << std::endl;
        }
    }

    outNode->deSerialize(j["outNode"]);
    if (j.contains("inNode")) inNode->deSerialize(j["inNode"]);
    std::cout << "[DBG_DESER]  outNode inputs=" << outNode->inputs.connections.size() << std::endl;

    for (auto s : j["connections"]) {
        Node* dstNode;
        int dstNodeID = s["dstNodeID"];
        if (dstNodeID == 0) dstNode = outNode;
        else if (dstNodeID == 1) dstNode = inNode;
        else dstNode = getNode(dstNodeID);

        auto dstConID = s["dstConID"];
        int srcNodeID = s["srcNodeID"];
        Node* srcNode = nullptr;
        if (srcNodeID == 0) srcNode = outNode;
        else if (srcNodeID == 1) srcNode = inNode;
        else srcNode = getNode(srcNodeID);
        auto srcConID = s["srcConID"];
        std::cout << "[DBG_DESER]  replay conn srcNode=" << s["srcNodeID"] << " srcCon=" << srcConID
                  << " -> dstNode=" << dstNodeID << " dstCon=" << dstConID
                  << " srcNodeExists=" << (srcNode ? 1 : 0) << " dstNodeExists=" << (dstNode ? 1 : 0)
                  << std::endl;
        if (!srcNode || !dstNode) {
            std::cerr << "[ERR] deSerialize connection: node not found src=" << srcNodeID << " dst=" << dstNodeID << std::endl;
            continue;
        }
        makeNodeConnectionNow(srcNode->id, srcConID, dstNode->id, dstConID);
    }
    outNode->makeConnectionRects();
    inNode->makeConnectionRects();
    for (auto n : nodes) n->makeConnectionRects();
    std::cout << "[DBG_DESER] NodeManager::deSerialize end path=";
    for (size_t i = 0; i < managerPath.size(); ++i) {
        std::cout << managerPath[i] << (i + 1 < managerPath.size() ? "/" : "");
    }
    if (managerPath.empty()) std::cout << "root";
    std::cout << std::endl;
    topologyDirty = true;
}

Node* NodeManager::getNode(uint16_t id) {
    if (id == 0) return outNode;
    if (id == 1) return inNode;
    auto it = ids.find(id);
    if (it == ids.end()) return nullptr;
    return nodes[it->second];
}

NodeManager::~NodeManager() {
    resetNE();
    for(auto n : nodes) {
        delete n;
    }
    delete outNode;
    delete inNode;

    nodes.clear();
}

std::vector<Node*> NodeManager::getNodes() {
    return nodes;
}

void NodeManager::markTopologyDirty() {
    topologyDirty = true;
}

void NodeManager::makeNodeConnection(
        Node* srcNode, uint16_t srcConID,
        Node* dstNode, uint16_t dstConID
) {
    auto pa = new MakeNodeConnectionAction(project, managerPath, srcNode->id, srcConID, dstNode->id, dstConID);
    project->um->newAction(pa);
}

void NodeManager::severConnection(Connection* c) {
    if (!c->is_connected) return;
    uint16_t srcNodeID, srcConID, dstNodeID, dstConID;
    if (c->dir == Direction::input) {
        dstNodeID = c->output_node;
        dstConID = c->output_connection;
        srcNodeID = c->input_node;
        srcConID = c->input_connection;
    } else {
        srcNodeID = std::numeric_limits<uint16_t>::max();
        srcConID = c->id;
        for (auto oc : outNode->outputs.connections) {
            if (oc == c) {
                srcNodeID = 0;
                break;
            }
        }
        if (srcNodeID == std::numeric_limits<uint16_t>::max()) {
            for (auto oc : inNode->outputs.connections) {
                if (oc == c) {
                    srcNodeID = 1;
                    break;
                }
            }
        }
        for (auto n : nodes) {
            for (auto oc : n->outputs.connections) {
                if (oc == c) {
                    srcNodeID = n->id;
                    break;
                }
            }
            if (srcNodeID != std::numeric_limits<uint16_t>::max()) break;
        }
        if (srcNodeID == std::numeric_limits<uint16_t>::max()) return;
        dstNodeID = c->output_node;
        dstConID = c->output_connection;
    }
    auto pa = new SeverNodeConnectionAction(project, managerPath, srcNodeID, srcConID, dstNodeID, dstConID);
    project->um->newAction(pa);
}

void NodeManager::addNode(NodeType t, float x, float y) {
    auto pa = new AddNodeAction(project, managerPath, t, x, y);
    if (ne) { pa->panOffX = ne->panOffsetX_; pa->panOffY = ne->panOffsetY_; }
    project->um->newAction(pa);
}

void NodeManager::removeNode(Node* n) {
    auto pa = new RemoveNodeAction(project, managerPath, n->id);
    if (ne) { pa->panOffX = ne->panOffsetX_; pa->panOffY = ne->panOffsetY_; }
    project->um->newAction(pa);
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

    if(update || topologyDirty) {
        inNode->update(bufferSize, sampleRate);
        for(auto node : nodes) {
            node->update(bufferSize, sampleRate);
        }
        outNode->numChannels = numChannels;
        outNode->update(bufferSize, sampleRate);

        // Second pass: relink all input pointers after every node has refreshed outputs.
        inNode->relinkInputs();
        for (auto node : nodes) {
            node->relinkInputs();
        }
        outNode->relinkInputs();
        topologyDirty = false;
    }

    outNode->output = output;
    outNode->processTree();
    outNode->resetProcessTree();
}

Node* NodeManager::addNodeNow(NodeType t, float x, float y, int forcedID) {
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
    if (ne) n->setNE(ne);
    n->setup();
    topologyDirty = true;
    return n;
}

Node* NodeManager::addNodeNow(json j) {
    uint16_t id = j["id"];
    id_pool.reserveID(id);
    auto node = Node::deSerialize(j, this);
    if (!node) return nullptr;
    nodes.push_back(node);
    ids[id] = nodes.size() - 1;
    node->update(bufferSize, sampleRate);
    if (ne) node->setNE(ne);
    topologyDirty = true;
    return node;
}

void NodeManager::removeNodeNow(uint16_t id) {
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
    if (ne) ne->clearPointersToNode(removed);
    delete removed;
    topologyDirty = true;
}

void NodeManager::makeNodeConnectionNow(uint16_t srcNodeID, uint16_t srcConID, uint16_t dstNodeID, uint16_t dstConID) {
    Node* srcNode = (srcNodeID == 0) ? static_cast<Node*>(outNode)
                    : (srcNodeID == 1) ? static_cast<Node*>(inNode)
                    : getNode(srcNodeID);
    Node* dstNode = (dstNodeID == 0) ? static_cast<Node*>(outNode)
                    : (dstNodeID == 1) ? static_cast<Node*>(inNode)
                    : getNode(dstNodeID);
    if (!srcNode || !dstNode) {
        std::cout << "[DBG_DESER] makeNodeConnectionNow skip missing node src=" << srcNodeID << " dst=" << dstNodeID << std::endl;
        return;
    }
    if (srcNode->depends(dstNode)) {
        std::cout << "[DBG_DESER] makeNodeConnectionNow skip cycle src=" << srcNodeID << " dst=" << dstNodeID << std::endl;
        return;
    }

    Connection* srcCon = srcNode->outputs.getConnection(srcConID);
    Connection* dstCon = dstNode->inputs.getConnection(dstConID);
    if (!srcCon || !dstCon) {
        std::cout << "[DBG_DESER] makeNodeConnectionNow skip missing con srcCon=" << srcConID << " dstCon=" << dstConID
                  << " srcNode=" << srcNodeID << " dstNode=" << dstNodeID << std::endl;
        return;
    }
    if (srcCon->type != dstCon->type || srcCon->is_connected || dstCon->is_connected) {
        std::cout << "[DBG_DESER] makeNodeConnectionNow skip state mismatch srcNode=" << srcNodeID
                  << " srcCon=" << srcConID << " dstNode=" << dstNodeID << " dstCon=" << dstConID
                  << " srcType=" << srcCon->type << " dstType=" << dstCon->type
                  << " srcConnected=" << srcCon->is_connected << " dstConnected=" << dstCon->is_connected
                  << std::endl;
        return;
    }

    dstCon->buffer = srcCon->buffer;
    dstCon->events = srcCon->events;
    dstCon->input_node = srcNode->id;
    dstCon->input_connection = srcConID;

    srcCon->output_node = dstNode->id;
    srcCon->output_connection = dstConID;
    srcCon->is_connected = true;
    dstCon->is_connected = true;
    std::cout << "[DBG_DESER] makeNodeConnectionNow ok srcNode=" << srcNodeID << " srcCon=" << srcConID
              << " -> dstNode=" << dstNodeID << " dstCon=" << dstConID << std::endl;
}

void NodeManager::severConnectionNow(uint16_t srcNodeID, uint16_t srcConID, uint16_t dstNodeID, uint16_t dstConID) {
    Node* srcNode = (srcNodeID == 0) ? static_cast<Node*>(outNode)
                    : (srcNodeID == 1) ? static_cast<Node*>(inNode)
                    : getNode(srcNodeID);
    Node* dstNode = (dstNodeID == 0) ? static_cast<Node*>(outNode)
                    : (dstNodeID == 1) ? static_cast<Node*>(inNode)
                    : getNode(dstNodeID);
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

bool NodeManager::snapshotNode(uint16_t nodeID, json& nodeData, json& connections) {
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

bool NodeManager::peekRemovableInputWaveform(uint16_t* outId, size_t* outIndex) {
    return inNode->peekLastRemovableWaveformOutput(outId, outIndex);
}

bool NodeManager::peekRemovableInputEvent(uint16_t* outId, size_t* outIndex) {
    return inNode->peekLastRemovableEventOutput(outId, outIndex);
}

bool NodeManager::peekRemovableOutputWaveform(uint16_t* outId, size_t* outIndex) {
    return outNode->peekLastRemovableWaveformInput(outId, outIndex);
}

bool NodeManager::peekRemovableOutputEvent(uint16_t* outId, size_t* outIndex) {
    return outNode->peekLastRemovableEventInput(outId, outIndex);
}
