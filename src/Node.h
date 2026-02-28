#pragma once
#include <vector>
#include <string>
#include "Bus.h"
#include "idManager.h"
#include <unordered_map>
#include <SDL3/SDL.h>
#include "SDL3_gfx/SDL3_gfxPrimitives.h"
#include "nodes/nodetype.h"
#include "TreeEntry.h"

#define TEX_W 1920
#define TEX_H 1080

struct sourceNode{
    ConnectionType type;
    uint16_t source_id;
    uint16_t output_id;
};

struct connectionSet{
    std::vector<Connection*> connections;
    idManager id_pool;

    std::unordered_map<uint16_t,uint16_t> ids;

    uint16_t getIndex(uint16_t id);
    Connection* getConnection(uint16_t id);

    void addConnection(Connection*);
    ~connectionSet();

    int nodeID = -1;
};

class Node {
    public:
        NodeType nodeType = NodeType::Count;

        Node(uint16_t, NodeType);
        virtual ~Node();

        uint16_t id;
        std::string name;

        connectionSet inputs;

        connectionSet outputs;

        void* getOutput(Connection*);
        void* getInput(Connection*);
        Node* getNodeInput(Connection*);

        EventBus* getEvents(void*);
        WaveformBus* getWaveform(void*);

        bool depends(Node*); // check if this node has another node as an input somewhere down the tree
        void processTree();
        bool isProcessed = false;
        void resetProcessTree();

        virtual void process() = 0;

        SDL_FRect dstRect;

        bool moving = false;
        void move(float, float);
        void resize(float, float);
        bool canZoom(float);
        void zoom(float);

        // bounding polygon (for gui)
        float* vx = nullptr; 
        float* vy = nullptr;
        // fixed size, no position
        size_t vCount = 0;

        float zoomRatio = 50.0f / TEX_H; // 50px for every TEX_H texture pixels, so 50px height default

        SDL_Texture* texture = nullptr;
        void renderContentHelper(SDL_Renderer*);
        virtual void renderContent(SDL_Renderer*);

        void render(SDL_Renderer*);

        int bufferSize;
        int sampleRate;

        virtual void setup();

        void update(int, int);

        float& mouseX;
        float& mouseY;

        bool& isAltPressed;
        bool& isCtrlPressed;

        int hoveredConnection;
        Direction hoveredDirection;

        bool handleInput(SDL_Event&);
        void clickMouse(SDL_Event&);

        std::shared_ptr<TreeEntry> getConnectionMenu(Connection*);

        json serialize();
        static Node* deSerialize(json);
}; 
