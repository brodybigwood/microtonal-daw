#pragma once

#include <vector>
#include <unordered_map>
#include "idManager.h"
#include <SDL3/SDL.h>
#include "OutputNode.h"
#include "nodes/nodetypes.h"

class Bus;
class Node;

class NodeManager {
    public:
        ~NodeManager();

        static NodeManager* get();

        Node* getNode(uint16_t);

        void removeNode(uint16_t);
        Node* addNode(NodeType);

        void process(float*, int&, int&, int&);

        void render(SDL_Renderer*, SDL_FRect*);

        void makeNodeConnection(
                Node*, uint16_t, 
                Node*, uint16_t
                ); 
        void makeBusConnection(Bus*, Node*, uint16_t);

        std::vector<Node*>& getNodes();

        OutputNode outNode;

    private:
        std::unordered_map<uint16_t, uint16_t> ids;
        idManager id_pool;

        std::vector<Node*> nodes;

        int bufferSize = 0;
        int sampleRate = 0;
        int numChannels = 0;
};
