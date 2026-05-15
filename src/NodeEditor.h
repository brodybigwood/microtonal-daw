#pragma once

#include <SDL3/SDL.h>
#include "TreeEntry.h"
#include "nodes/nodetypes.h"
#include "Window.h"
#include "Bus.h"


#define SINE_SIZE 2000

class Node;

class NodeEditor : public Window {
    public:
        NodeEditor();
        ~NodeEditor();

        NodeManager* nm;

        void tick();
        void handleInput(SDL_Event&);
        void handleWindowInput(SDL_Event& e) override { handleInput(e); }

        uint32_t getWindowID();
        void renderPresent();

        float mouseX = 0;
        float mouseY = 0;
        bool leftClick = false;
    
        void setDstConn(Node*, int);
        void setSrcConn(Node*, int);
        void setMovingNode(Node*);
        void releaseMovingNode(bool commitAction = true);
        void cancelMovingNode();

        bool& isAltPressed;
        bool& isCtrlPressed;

        SDL_Window* getWindow() { return window; };
        SDL_Renderer* getRenderer() { return renderer; };

        void retach();

        int windowWidth = 1920;
        int windowHeight = 1080;
        SDL_FRect nodeRect{0, 0, 1920, 1080};

        /** Full patch canvas (menu draws in the top band; `nodeRect` is the graph area below when the menu is on). */
        void setEmbeddedCanvasSize(float w, float h);

        /** Only `PatcherNode::detachFinal` sets this on `mainEditor` (top-level patcher = host lives on canvas with empty `managerPath`). */
        void setTopMenuBarHostNode(Node* hostPatchNode) { menuBarHostNode_ = hostPatchNode; }

        /** Called from `NodeManager::setNE` / `resetNE` only (menu bar layout for hosted patch editor). */
        void updateRootMenuBarLayout();
        void resetRootMenuBarLayout();

        /** Draw curved patch cable preview; uses `r` so it matches the same render target as socket drawing. */
        static void renderPatchCable(SDL_Renderer* r, float x1, float y1, float x2, float y2, SDL_FColor color);

        void renderSine(float x1, float y1, float x2, float y2, SDL_FColor);
        /** Invalidate wire/drag pointers before `n` is deferred-deleted or replaced by undo. */
        void clearPointersToNode(Node* n);

        /** Clear all in-progress connection / drag state (e.g. before destroying this editor). */
        void clearWireDragState();



    private:

        void zoom(float);
        void move();

        float topMargin = 0.0f;
        float leftMargin = 0.0f;

        uint32_t lastLeftClick;

        void doubleClick();
        void createNode(NodeType);

        void moveMouse();
        void clickMouse(SDL_Event&);
        void keydown(SDL_Event&);

        Node* hoveredNode = nullptr;

        Node* dstNode = nullptr;
        int dstNodeID = -1;

        Node* srcNode = nullptr;
        int srcNodeID = -1;

        void makeConnection();
        void renderConnector(SDL_Renderer*);

        void hover();
        /** `surfaceRect` is the full canvas (0,0,canvas); menu then everything in `nodeRect` (clipped). */
        void render(SDL_Renderer*, SDL_FRect* surfaceRect);

        /** Top menu strip (skeleton) when `menuBarHostNode_` marks a top-level patcher editor. */
        void renderRootMenuBarSkeleton(SDL_Renderer*, const SDL_FRect* surfaceRect);

        bool isPointerOverMenuBar(float mx, float my) const;

        static constexpr float kRootMenuBarStripH = 22.0f;

        bool inside(float&, float&, SDL_FRect*);

        std::shared_ptr<TreeEntry> getClickMenu();

        Node* movingNode = nullptr;
        float movingNodeStartX = 0.0f;
        float movingNodeStartY = 0.0f;
        float moveOffX;
        float moveOffY;

        /** Patcher node that owns this editor when embedded; null for processor host / other editors. */
        Node* menuBarHostNode_ = nullptr;

        /** Which top-level menu label has its dropdown open (-1 = none). Index into kLabels (File=0, Edit=1, View=2, Window=3). */
        int menuOpenIndex_ = -1;
        /** Rects of the 4 menu-bar label cells, populated each render frame for hit-testing. */
        std::array<SDL_FRect, 4> menuLabelRects_{};

        /** Build the tree for a top-menu dropdown (delegates to ContextMenu for display). */
        std::shared_ptr<TreeEntry> buildMenuTree(int menuIndex);

        float canvasW_ = 1920.f;
        float canvasH_ = 1080.f;


};
