#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include "idManager.h"
#include <array>
#include "TreeEntry.h"
#include "nodes/nodetypes.h"
#include "Bus.h"
#include <unordered_map>
#include "EmbeddedWindow.h"

#define SINE_SIZE 2000

class Node;
class PreferencesWindow;
class UndoTreeWindow;

class NodeEditor {
    public:
        NodeEditor();
        ~NodeEditor();

        NodeManager* nm;

        void tick(SDL_Renderer* r);
        void handleInput(SDL_Event&);
        void handleWindowInput(SDL_Event& e) { handleInput(e); }

        void renderPresent(SDL_Renderer* renderer);

        float mouseX = 0;
        float mouseY = 0;
        bool leftClick = false;

        void setDstConn(Node*, int);
        void setSrcConn(Node*, int);

        /** Start dragging from a port (new or existing connection). */
        void startPortDrag(Node* node, int portId, Direction dir);

        /** Finish drag: sever, reassign, or create based on what's under the mouse. */
        void handleDragDrop();

        /** Check whether a connection's normal cable should be suppressed during drag. */
        bool isConnectionBeingDragged(Connection* c) const;

        bool& isAltPressed;
        bool& isCtrlPressed;
        bool& isShiftPressed;


        void retach();

        int windowWidth = 1920;
        int windowHeight = 1080;
        SDL_FRect nodeRect{0, 0, 1920, 1080};

        /** Full patch canvas (menu draws in the top band; `nodeRect` is the graph area below when the menu is on). */
        void setEmbeddedCanvasSize(float w, float h);

        /** Only `PatcherNode::detachFinal` sets this on `mainEditor` (top-level patcher = host lives on canvas with empty `managerPath`). */
        void setTopMenuBarHostNode(Node* hostPatchNode) { menuBarHostNode_ = hostPatchNode; }

        /** Enable the root menu bar directly (for the top-level editor, not hosted by a patcher). */
        void enableRootMenuBar() { rootMenuBar_ = true; }

        /** When embedded, moveMouse() skips SDL_GetMouseState; caller sets mouseX/mouseY directly. */
        bool embedded_ = false;

        /** Called from `NodeManager::setNE` / `resetNE` only (menu bar layout for hosted patch editor). */
        void updateRootMenuBarLayout();
        void resetRootMenuBarLayout();

        /** Draw curved patch cable preview; uses `r` so it matches the same render target as socket drawing. */
        static void renderPatchCable(SDL_Renderer* r, float x1, float y1, float x2, float y2, SDL_FColor color);

        void renderSine(SDL_Renderer*, float x1, float y1, float x2, float y2, SDL_FColor);
        /** Invalidate wire/drag pointers before `n` is deferred-deleted or replaced by undo. */
        void clearPointersToNode(Node* n);

        /** Clear all in-progress connection / drag state (e.g. before destroying this editor). */
        void clearWireDragState();

        // --- Embedded window management ---

        EmbeddedWindow* addEmbeddedWindow(std::unique_ptr<EmbeddedWindow> w);
        void removeEmbeddedWindow(EmbeddedWindow* w);
        void clearPointersToEmbeddedWindow(EmbeddedWindow* w);

        void renderEmbeddedWindows(SDL_Renderer* r);

        bool routeEmbeddedWindowEvent(SDL_Event& e, float mouseX, float mouseY);

        PreferencesWindow* existingPreferencesWindow();

        UndoTreeWindow* existingUndoTreeWindow();

        EmbeddedWindow* focusedEmbeddedWindow() const { return focusedEmbeddedWindow_; }

        void registerEmbeddedWindow(EmbeddedWindow* ew) {
            if (ew->id >= 0) {
                ewIdPool_.reserveID(static_cast<uint16_t>(ew->id));
                embeddedWindowById_[ew->id] = ew;
            } else {
                ew->id = static_cast<int>(ewIdPool_.newID());
                embeddedWindowById_[ew->id] = ew;
            }
            std::cerr << "[EWREG] id=" << ew->id << " ptr=" << ew << std::endl;
        }
        void unregisterEmbeddedWindow(EmbeddedWindow* ew) {
            if (ew->id >= 0) {
                embeddedWindowById_.erase(ew->id);
                ewIdPool_.releaseID(static_cast<uint16_t>(ew->id));
                ew->id = -1;
            }
        }
        void reserveEwID(uint16_t id) { ewIdPool_.reserveID(id); }
        json ewIdPoolToJSON() { return ewIdPool_.toJSON(); }
        void ewIdPoolFromJSON(const json& j) { ewIdPool_.fromJSON(j); }
        EmbeddedWindow* getEmbeddedWindowById(int id) {
            auto it = embeddedWindowById_.find(id);
            return it != embeddedWindowById_.end() ? it->second : nullptr;
        }
        json serializeOpenPianoRolls(const std::vector<int>& managerPath) const;
        void restoreOpenPianoRolls(const json& arr);

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

        /** True when this is the top-level processor editor (not embedded in any patcher). */
        bool rootMenuBar_ = false;

        /** Which top-level menu label has its dropdown open (-1 = none). Index into kLabels (File=0, Edit=1, View=2, Window=3). */
        int menuOpenIndex_ = -1;
        /** Rects of the 4 menu-bar label cells, populated each render frame for hit-testing. */
        std::array<SDL_FRect, 4> menuLabelRects_{};

        /** Build the tree for a top-menu dropdown (delegates to ContextMenu for display). */
        std::shared_ptr<TreeEntry> buildMenuTree(int menuIndex);

        std::vector<std::unique_ptr<EmbeddedWindow>> embeddedWindows_;
        std::unordered_map<int, EmbeddedWindow*> embeddedWindowById_;
        idManager ewIdPool_;
        enum class CaptureKind : uint8_t { None, Resize, Content, Connection };
        EmbeddedWindow* capturedEmbeddedWindow_ = nullptr;
        CaptureKind captureKind_ = CaptureKind::None;
        EmbeddedWindow* focusedEmbeddedWindow_ = nullptr;

        // Undo tracking for embedded window drag/resize.
        float undoBeforeX_ = 0.f, undoBeforeY_ = 0.f, undoBeforeW_ = 0.f, undoBeforeH_ = 0.f;
        bool undoIsResize_ = false;
        bool undoCaptured_ = false;

        float canvasW_ = 1920.f;
        float canvasH_ = 1080.f;

        // Ctrl+pan undo tracking
        float panStartX_ = 0.f;
        float panStartY_ = 0.f;
        bool panning_ = false;

        SDL_Cursor* currentCursor_ = nullptr;

        // Drag-to-sever/reassign/create state
        bool dragInProgress_ = false;
        bool dragIsNew_ = false;           // true when dragging from an unconnected port
        Node* dragNode_ = nullptr;         // port being dragged (clicked)
        int dragPort_ = -1;
        Direction dragDir_ = Direction::input;
        Node* dragAnchorNode_ = nullptr;   // other end of existing connection
        int dragAnchorPort_ = -1;
        Connection* draggedConnection_ = nullptr; // the existing connection being dragged

        /** Find a port under (mx, my). Returns the node, sets portId and portDir. */
        Node* findPortAt(float mx, float my, int& portId, Direction& portDir);

    public:
        float panOffsetX_ = 0.f;
        float panOffsetY_ = 0.f;

    private:

};
