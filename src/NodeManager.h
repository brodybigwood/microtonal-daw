#pragma once

#include <vector>
#include <unordered_map>
#include <functional>
#include "idManager.h"
#include <SDL3/SDL.h>
#include "OutputNode.h"
#include "InputNode.h"
#include "nodes/nodetypes.h"

class Node;
class NodeEditor;

class NodeManager {
    public:
        NodeManager(Project*, std::vector<int> managerPath = {});
        ~NodeManager();

        json serialize();
        void deSerialize(json);
        /** Replace every user node/cable with a saved patcher preset. The
            manager's own Input/Output nodes are kept (their port state is
            reloaded from the preset). */
        void applyGraphPreset(const json&);
       
        static NodeManager* get();

        Node* getNode(uint16_t);

        // Nodes that live outside the processed node list but belong to this manager's
        // id namespace (mixer chain inserts), so actions can address them normally.
        void registerExtraNode(Node* n);
        void unregisterExtraNode(Node* n);
        /** Allocates an id not used by any node or extra node in this manager. */
        uint16_t allocateNodeID();
        void reserveNodeID(uint16_t id);

        void removeNode(Node*);
        void addNode(NodeType, float x, float y);

        void process(float*, int&, int&, int&);

        void render(SDL_Renderer*, SDL_FRect*);

        void makeNodeConnection(Node*, uint16_t, Node*, uint16_t);
        void severConnection(Connection*);

        // Graph mutation methods (called by action lambdas)
        Node* addNodeNow(NodeType, float, float, int forcedID = -1);
        Node* addNodeNow(json);
        void removeNodeNow(uint16_t);
        void makeNodeConnectionNow(uint16_t, uint16_t, uint16_t, uint16_t);
        void severConnectionNow(uint16_t, uint16_t, uint16_t, uint16_t);
        bool snapshotNode(uint16_t, json&, json&);

        bool peekRemovableInputWaveform(uint16_t* outId, size_t* outIndex);
        bool peekRemovableInputEvent(uint16_t* outId, size_t* outIndex);
        bool peekRemovableOutputWaveform(uint16_t* outId, size_t* outIndex);
        bool peekRemovableOutputEvent(uint16_t* outId, size_t* outIndex);

        std::vector<Node*> getNodes();

        void markTopologyDirty();

        // Forward channel-width propagation for the GUI graph (no buffer allocation).
        void refreshChannelWidths();

        Project* project;

        void setNE(NodeEditor*);
        void resetNE();
        NodeEditor* ne = nullptr;
        OutputNode* outNode;
        InputNode* inNode;
        std::vector<int> managerPath;

    private:
        std::unordered_map<uint16_t, uint16_t> ids;
        std::unordered_map<uint16_t, Node*> extraNodes;
        idManager id_pool;

        std::vector<Node*> nodes;

        int bufferSize = 0;
        int sampleRate = 0;
        int numChannels = 0;
        bool topologyDirty = false;
};
