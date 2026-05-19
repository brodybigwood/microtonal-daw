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
#include "Parameter.h"
#include "Geometry.h"
#include "Project.h"
#include "EmbeddedWindow.h"

#define TEX_W 1280
#define TEX_H 720

class NodeEditor;

struct connectionSet{
    std::vector<Connection*> connections;
    idManager id_pool;

    std::unordered_map<uint16_t,uint16_t> ids;

    uint16_t getIndex(uint16_t id);
    Connection* getConnection(uint16_t id);

    void addConnection(Connection*);
    ~connectionSet();

    int nodeID = -1;
    NodeManager* nm;
    int bufferSize = 0;
};

class Node : public EmbeddedWindow {
    public:
        NodeType nodeType = NodeType::Count;

        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;

        Project* project;
        NodeManager* nm;
        NodeEditor* ne;

        Node(uint16_t, NodeManager* nm, NodeType);
        virtual ~Node();

        uint16_t id;
        std::string name;

        connectionSet inputs;

        connectionSet outputs;

        void* getOutput(Connection*);
        void* getInput(Connection*);
        Node* getNodeInput(Connection*);

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
        void makeConnectionRects();

        // bounding polygon (for gui)
        float* vx = nullptr;
        float* vy = nullptr;
        // fixed size, no position
        size_t vCount = 0;

        float zoomRatio = 50.0f / TEX_H; // 50px for every TEX_H texture pixels, so 50px height default

        SDL_Texture* texture = nullptr;
        void renderContentHelper(SDL_Renderer*);
        virtual void renderContent(SDL_Renderer*);

        void render();
        virtual void renderPresent() {}

        std::vector<Parameter*> params;
        void renderParams(SDL_Renderer*);

        int bufferSize;
        int sampleRate;

        virtual void setup();
        void relinkInputs();

        void update(int, int);

        float* mouseX;
        float* mouseY;

        float msX;
        float msY;

        bool& isAltPressed;
        bool& isCtrlPressed;

        int hoveredConnection;
        Direction hoveredDirection;

        uint32_t lastLeftClick;
        bool handleContentInput(SDL_Event&) override;
        virtual bool handleCustomInput(SDL_Event&) { return false; }
        virtual bool blocksDoubleClick(float, float) const { return false; }
        void clickMouse(SDL_Event&);

        std::shared_ptr<TreeEntry> getConnectionMenu(Connection*);
        std::shared_ptr<TreeEntry> getParameterMenu(Parameter*, const std::vector<size_t>& path = {});
        std::shared_ptr<TreeEntry> getNodeMenu();

        // Resolve a nested parameter path: {paramIndex, modIdx0, modIdx1, ...}
        Parameter* resolveParameterPath(const std::vector<size_t>& path);
        std::string parameterPathLabel(const std::vector<size_t>& path) const;

        // Path-based (primary)
        void addModSource(const std::vector<size_t>& path);
        void removeModSource(const std::vector<size_t>& path, size_t modIndex);
        bool addModSourceNow(const std::vector<size_t>& path);
        bool removeModSourceNow(const std::vector<size_t>& path, size_t modIndex);

        // Convenience for top-level params (looks up paramIndex, delegates to path version)
        void addModSource(Parameter*);
        void removeModSource(Parameter*, size_t);

        json serialize();
        static Node* deSerialize(json, NodeManager*);

        virtual json extraSerialize() { json j; return j; }
        virtual void extraDeSerialize(json j) {}

        void clearTextures();
        void clearParamTextures();
        virtual void clearCustomTextures() {}

        void attach();

        void moveTo(float nx, float ny) override {
            EmbeddedWindow::moveTo(nx, ny);
            dstRect.x = nx;
            dstRect.y = ny;
        }

        void applyGeometry(float nx, float ny, float nw, float nh) override {
            x = nx; y = ny; w = nw; h = nh;
            zoomRatio = nw / TEX_W;
            dstRect = {nx, ny, nw, nh};
            markPolygonDirty();
            makeConnectionRects();
        }

        virtual void handleWindowInput(SDL_Event&);

        void setNE(NodeEditor*);
        void resetNE();

        virtual void setNEFinal() {}
        virtual void resetNEFinal() {}

        // EmbeddedWindow polygon: use the node's existing shape.
        bool hasRectResize() const override { return false; }
        float minW() const override { return 40.f; }
        float minH() const override { return 22.5f; } // 40 * TEX_H/TEX_W
        void buildHitPolygon(std::vector<SDL_FPoint>& out) const override;
        void applyResizeDelta(float dx, float dy) override;
}; 
