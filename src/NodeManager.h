#pragma once

#include <vector>
#include <unordered_map>
#include "idManager.h"

class Node;

class NodeManager {
    public:
        ~NodeManager();

        static NodeManager* get();

        Node* getNode(uint16_t);

        void removeNode(uint16_t);
        Node* addNode();

        void process(float*, int);

    private:
        std::unordered_map<uint16_t, uint16_t> ids;
        idManager id_pool;

        std::vector<Node*> nodes;
};
