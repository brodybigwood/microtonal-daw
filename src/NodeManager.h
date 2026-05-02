#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>
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
       
        static NodeManager* get();

        Node* getNode(uint16_t);

        // UI-thread requests (wrapped as UndoManager actions)
        void removeNode(Node*);
        void addNode(NodeType, float x, float y);

        void process(float*, int&, int&, int&);
        void flushUiDeferred();

        void render(SDL_Renderer*, SDL_FRect*);

        void makeNodeConnection(Node*, uint16_t, Node*, uint16_t);
        void severConnection(Connection*);

        // Audio-thread execution methods (called by action lambdas)
        Node* addNodeNow(NodeType, float, float, int forcedID = -1);
        Node* addNodeNow(json);
        void removeNodeNow(uint16_t);
        void makeNodeConnectionNow(uint16_t, uint16_t, uint16_t, uint16_t);
        void severConnectionNow(uint16_t, uint16_t, uint16_t, uint16_t);
        void moveNodeNow(uint16_t, float, float);
        bool snapshotNode(uint16_t, json&, json&);

        /** Peek last removable socket (under graph lock). Used when building remove undo actions. */
        bool peekRemovableInputWaveform(uint16_t* outId, size_t* outIndex);
        bool peekRemovableInputEvent(uint16_t* outId, size_t* outIndex);
        bool peekRemovableOutputWaveform(uint16_t* outId, size_t* outIndex);
        bool peekRemovableOutputEvent(uint16_t* outId, size_t* outIndex);

        std::vector<Node*> getNodes();

        void markTopologyDirty();
        void runWithGraphLock(const std::function<void()>& fn);

        Project* project;
        
        void setNE(NodeEditor*);
        void resetNE();
        NodeEditor* ne = nullptr;
        OutputNode* outNode;
        InputNode* inNode;
        std::vector<int> managerPath;
        PortDisplayMode portDisplayMode = PortDisplayMode::RectLabels;

    private:
        std::unordered_map<uint16_t, uint16_t> ids;
        idManager id_pool;

        std::vector<Node*> nodes;

        int bufferSize = 0;
        int sampleRate = 0;
        int numChannels = 0;
        bool topologyDirty = false;

        std::mutex deferredDeleteMutex;
        std::vector<Node*> deferredDeleteNodes;
        mutable std::recursive_mutex graphMutex;
};
