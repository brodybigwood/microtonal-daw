#include "NodeManager.h"
#include "Node.h"


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

Node* NodeManager::addNode() {
    uint16_t id = id_pool.newID();
    Node* n = new Node(id);
    nodes.push_back(n);
    ids[id] = nodes.size() - 1;
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
