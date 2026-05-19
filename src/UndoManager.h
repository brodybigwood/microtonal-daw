#pragma once

#include "Region.h"
#include "ElementManager.h"
#include "Parameter.h"
#include "Bus.h"
#include <mutex>
#include <string>
#include <unordered_map>

class Project;
class ArrangerNode;

/** Resolve arranger region for undo/redo (same addressing as note actions). */
Region* undoResolveArrangerRegion(Project* p, const std::vector<int>& managerPath, int nodeID, int regionID);
ArrangerNode* undoResolveArrangerNode(Project* p, const std::vector<int>& managerPath, int nodeID);
ElementManager* undoResolveArrangerElementManager(Project* p, const std::vector<int>& managerPath, int nodeID);

enum ActionType {
    CreateNote = 0,
    AddArrangerTrack = 1,
    MoveNode = 2,
    AddNode = 3,
    RemoveNode = 4,
    MakeNodeConnection = 5,
    SeverNodeConnection = 6,
    UndoHead = 7,
    MoveNote = 8,
    DeleteNote = 9,
    PianoRollRegionTuning = 10,
    AssignNoteHarmonic = 11,
    AddModSourceUndo = 12,
    RemoveModSourceUndo = 13,
    CreateRegion = 14,
    DeleteRegion = 15,
    CreatePosition = 16,
    DeletePosition = 17,
    MoveElementPosition = 18,
    IoPortChannel = 19,
    SetParamValue = 20,
    ToggleModulatorCentered = 21,
    MoveEmbeddedWindow = 22,
    ResizeEmbeddedWindow = 23
};


struct ProjectAction {
    ActionType type;

    std::function<void()> doAction;
    std::function<void()> undoAction;
    /** If true, `UndoManager::newAction` does not run `doAction` immediately (state already matches do). */
    bool skipInitialDo = false;

    std::vector<ProjectAction*> children;
    
    void newAction(ProjectAction* pa) {
        pa->index = children.size();
        children.push_back(pa);
        pa->parent = this;
    }    

    ProjectAction* parent = nullptr;
    int index;
    int last_index = 0;

    std::vector<int> version() {
        std::vector<int> v;
        if (parent) {
            v.insert(v.begin(), index); // which branch this is
            auto vp = parent->version(); // get parent version chain
            v.insert(v.begin(), vp.begin(), vp.end()); // put it before this version
        }
        return v;
    }

    static json serialize(ProjectAction*);
    static ProjectAction* deSerialize(json, Project*);

    Project* p;

    ProjectAction(Project* p, ActionType type) : p(p), type(type) {
        doAction = []() {};
        undoAction = []() {};
    }

    bool open = false;
    int hoveredIndex = -1;

    std::string name;
    SDL_Texture* texture = nullptr;

    /** Undo-tree window: show child rows; persisted per node in save files. */
    bool undoTreeExpanded = false;
};

struct UndoHeadAction : ProjectAction {
    explicit UndoHeadAction(Project* p) : ProjectAction(p, UndoHead) {}
};

struct UndoManager {
    ProjectAction* head;
    ProjectAction* current;

    std::mutex audioSyncMutex;
    std::vector<std::function<void()>> pendingAudioSync;

    void enqueueAudioSync(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(audioSyncMutex);
        pendingAudioSync.push_back(std::move(fn));
    }

    void flushAudioSync() {
        std::vector<std::function<void()>> actions;
        {
            std::lock_guard<std::mutex> lock(audioSyncMutex);
            actions.swap(pendingAudioSync);
        }
        for (auto& fn : actions) fn();
    }

    UndoManager(Project* p) {
        head = new UndoHeadAction(p);
        current = head;
    }

    void newAction(ProjectAction* pa);

    json serialize() {
        json j;
        j["head"] = ProjectAction::serialize(head);
        j["version"] = current->version();
        return j;
    }

    void deSerialize(json j, Project* p) {
        if (head) delete head;
        undoTreeViewRoot = nullptr;
        head = ProjectAction::deSerialize(j["head"], p);
        current = head;
        const std::vector<int> version = j.at("version").get<std::vector<int>>();
        for (int i : version) {
            current = current->children[static_cast<size_t>(i)];
        }
    }

    void undo();

    /** @param childIndex branch to redo; -1 uses `current->last_index` (clamped to valid range). */
    void redo(int childIndex = -1);

    void goTo(ProjectAction*);
    /** Undone at start of each undo-tree `render`: left = navigate to row, right = toggle expand (non-leaf). */
    bool undoTreePendingLeft = false;
    bool undoTreePendingRight = false;
    bool undoTreeFrameLeft = false;
    bool undoTreeFrameRight = false;
    ProjectAction* undoTreeHitUnderCursor = nullptr;

    /** When non-null, hit-testing for the undo tree uses client coords of this window instead of `MouseOn`. */
    SDL_Window* hitTestWindow = nullptr;

    SDL_FRect* baseRect = nullptr;
    /** Vertical size of each tree row when rendering; horizontal layout width uses `baseRect->w`. */
    float undoTreeRowH = 20.f;
    /** Optional top of the drawn subtree; nullptr means the real undo `head`. Wheel scroll shifts this toward parent/child. */
    ProjectAction* undoTreeViewRoot = nullptr;

    bool mouseHitsRect(SDL_FRect* rect) const;
    /** Window-client hit test (`mouse_xy` vs this window): positive wheel Y scrolls deeper, negative moves view toward parent. */
    void undoTreeHandleWheel(const SDL_FRect& layoutAnchor, float rowPixels, float mouseX, float mouseY, float wheelY);
    void clearAllRenderTextures();
    bool render(SDL_Renderer*);

    /** Ensures every ancestor of `current` is expanded so the tip row is reachable in the tree view. */
    void syncUndoTreeExpansionPathToCurrent();
    void applyUndoTreeClickAfterLayout();
    bool drawUndoTreeRow(SDL_Renderer* renderer, SDL_FRect* rect, ProjectAction* pa);

    struct UndoTreeLayoutBox {
        bool hovering = false;
        float bottomY = 0.0f;
    };
    UndoTreeLayoutBox layoutUndoTreeGeom(SDL_Renderer* renderer, float x, float y, float w, float rowH, ProjectAction* pa);

    static const std::unordered_map<std::string, ActionType>& actionRegistry();
    static std::string actionSchema(const std::string& actionName);
    bool runRegisteredAction(const std::string& actionName, const json& params, std::string& error);
};

struct PianoRollRegionTuningUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = 0;
    json beforeRegion;
    json afterRegion;

    PianoRollRegionTuningUndoAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, json beforeRegion,
                                    json afterRegion, std::string actionName);
};

struct AssignNoteHarmonicUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = 0;
    int noteID = 0;
    json beforeRegion;
    json afterRegion;
    json beforeNote;
    json afterNote;

    AssignNoteHarmonicUndoAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID,
                                 json beforeRegion, json afterRegion, json beforeNote, json afterNote);
};

struct AddModSourceUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    std::vector<size_t> paramPath;

    AddModSourceUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath);
};

struct RemoveModSourceUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    std::vector<size_t> paramPath;
    size_t modIndex = 0;

    /** Saved modulator subtree (removed from the parameter but kept alive here for undo). */
    Modulator* savedModulator = nullptr;
    /** Connections owned by the saved modulator tree, with their original indices and wiring. */
    struct SavedConn {
        Connection* conn;
        size_t index;
        bool wasConnected;
        uint16_t srcNode, srcCon;
    };
    std::vector<SavedConn> savedConns;

    RemoveModSourceUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath, size_t modIndex);
};

struct SetParamValueUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    std::vector<size_t> paramPath;
    float oldValue = 0.0f;
    float newValue = 0.0f;

    SetParamValueUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath,
                            float oldValue, float newValue, std::string actionName = "Set Param Value");
};

struct ToggleModulatorCenteredUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    std::vector<size_t> paramPath;
    size_t modIndex = 0;
    bool oldCentered = false;
    bool newCentered = false;
    float oldDepth = 0.0f;
    float newDepth = 0.0f;

    ToggleModulatorCenteredUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath,
                                      size_t modIndex, bool oldCentered, bool newCentered, float oldDepth, float newDepth);
};

struct MoveNoteAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = 0;
    int noteID = 0;
    json before;
    json after;

    MoveNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID, json before,
                   json after, std::string actionName = "Move Note");
};

struct DeleteNoteAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = 0;
    int noteID = 0;
    size_t insertIndex = 0;
    json noteSnapshot;

    DeleteNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID);
    /** Restored from project file (skips live snapshot). */
    DeleteNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID, size_t insertIndex,
                     json noteSnapshot);

private:
    void wireDeleteLambdas();
};

struct CreateNoteAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID;
    int noteID;
    int regionID;
    fract start;
    fract length;
    float pitch;
    std::vector<std::pair<int, int>> pitchIntegerPairs;
    /** Filled after PianoRoll::stampNoteTuning on first create; reapplied on redo (doAction) so tuningMode matches the lattice. */
    json noteStampedSnapshot = json();

    CreateNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, fract start, fract length,
                     float pitch, std::vector<std::pair<int, int>> pitchIntegerPairs);
};

struct CreateRegionAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = -1;
    json regionSnapshot = json::object();
    bool snapshotValid = false;

    /** Creates an empty region (no timeline positions). */
    CreateRegionAction(Project* p, std::vector<int> managerPath, int nodeID);
    /** Restored from project / undo JSON (`skipInitialDo`, graph already matches "after" for current pointer). */
    CreateRegionAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, json regionSnapshot);
};

struct CreatePositionAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int elementID = 0;
    fract start;
    uint16_t trackID = 0;
    int positionID = -1;

    CreatePositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, fract start, uint16_t trackID);
};

struct DeletePositionAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int elementID = 0;
    int positionID = 0;
    size_t insertIndex = 0;
    json positionSnapshot;

    DeletePositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, int positionID);
    DeletePositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, int positionID, size_t insertIndex,
                         json positionSnapshot);

private:
    void wireLambdas();
};

struct MoveElementPositionAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int elementID = 0;
    int positionID = 0;
    json before;
    json after;

    MoveElementPositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, int positionID, json before,
                              json after);
};

struct DeleteRegionAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = 0;
    size_t elementInsertIndex = 0;
    json regionSnapshot;

    DeleteRegionAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID);
    DeleteRegionAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, size_t elementInsertIndex, json regionSnapshot);

private:
    void wireDeleteRegionLambdas();
};

struct AddArrangerTrackAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID;
    int trackType;
    int trackID = -1;
    int connectionID = -1;

    AddArrangerTrackAction(Project* p, std::vector<int> managerPath, int nodeID, int trackType);
};

struct MoveEmbeddedWindowAction : ProjectAction {
    std::vector<int> managerPath;
    int ewID;
    float fromX, fromY;
    float toX, toY;

    MoveEmbeddedWindowAction(Project* p, std::vector<int> managerPath, int ewID, float fromX, float fromY, float toX, float toY);
};

struct ResizeEmbeddedWindowAction : ProjectAction {
    std::vector<int> managerPath;
    int ewID;
    float fromX, fromY, fromW, fromH;
    float toX, toY, toW, toH;

    ResizeEmbeddedWindowAction(Project* p, std::vector<int> managerPath, int ewID,
        float fromX, float fromY, float fromW, float fromH,
        float toX, float toY, float toW, float toH);
};

struct AddNodeAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeType;
    float x;
    float y;
    int nodeID = -1;
    /** After undo(removal), redo must restore snapshot (ports, patcher internals, layout) — not a fresh empty node. */
    bool hasRedoRestore = false;
    json redoNodeSnapshot;
    json redoConnectionsSnapshot = json::array();

    AddNodeAction(Project* p, std::vector<int> managerPath, int type, float x, float y);
};

struct RemoveNodeAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID;
    json nodeData;
    json connectionsData;

    RemoveNodeAction(Project* p, std::vector<int> managerPath, int nodeID);
};

struct MakeNodeConnectionAction : ProjectAction {
    std::vector<int> managerPath;
    int srcNodeID;
    int srcConID;
    int dstNodeID;
    int dstConID;

    MakeNodeConnectionAction(Project* p, std::vector<int> managerPath, int srcNodeID, int srcConID, int dstNodeID, int dstConID);
};

struct SeverNodeConnectionAction : ProjectAction {
    std::vector<int> managerPath;
    int srcNodeID;
    int srcConID;
    int dstNodeID;
    int dstConID;

    SeverNodeConnectionAction(Project* p, std::vector<int> managerPath, int srcNodeID, int srcConID, int dstNodeID, int dstConID);
};

namespace IoPortChannelOp {
inline constexpr int InputAddWaveform = 0;
inline constexpr int InputRemoveWaveform = 1;
inline constexpr int InputAddEvent = 2;
inline constexpr int InputRemoveEvent = 3;
inline constexpr int OutputAddWaveform = 4;
inline constexpr int OutputRemoveWaveform = 5;
inline constexpr int OutputAddEvent = 6;
inline constexpr int OutputRemoveEvent = 7;
} // namespace IoPortChannelOp

/** In/Out node ± socket changes (waveform + event buses). `connectionId` / `connectionIndex` are set after do for add ops, or snapshotted in ctor for remove ops. */
struct IoPortChannelAction : ProjectAction {
    std::vector<int> managerPath;
    int op = 0;
    uint16_t connectionId = 0;
    size_t connectionIndex = 0;
    /** For add-* ops: false until first do assigns connectionId (handles id 0 from id pool). */
    bool idAssigned = false;

    IoPortChannelAction(Project* p, int op, std::vector<int> managerPath, uint16_t connectionId, size_t connectionIndex);
};
