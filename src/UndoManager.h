#pragma once

#include "Region.h"
#include "ElementManager.h"
#include "Parameter.h"
#include "Bus.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <iostream>

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
    CreateRegion = 14,
    DeleteRegion = 15,
    CreatePosition = 16,
    DeletePosition = 17,
    MoveElementPosition = 18,
    IoPortChannel = 19,
    SetParamValue = 20,
    MoveEmbeddedWindow = 22,
    ResizeEmbeddedWindow = 23,
    ToggleNodeVisible = 24,
    PanNodes = 25,
    ZoomNodes = 26,
    AddEQBand = 27,
    RemoveEQBand = 28,
    VstParameterChange = 29,
    VstLoadPlugin = 30,
    TogglePianoRollWindow = 31,
    ReassignNodeConnection = 32,
    RemoveArrangerTrack = 33,
    MoveMultipleNotes = 34,
    SongRollRhythmEdo = 35,
    CreateAutomationCurve = 36,
    ModifyCurvePoints = 37,
    CreateAudioClip = 38,
    MapParameter = 39,
    UnmapParameter = 40,
    ParamNodeAddModRow = 41,
    ParamNodeRemoveModRow = 42,
    ParamNodeToggleCentered = 43,
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

    /** NodeManager snapshot stored only on the current action at save time. */
    json savedMainManager;

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

    virtual ~ProjectAction() = default;

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
        // Runs on the audio thread — an escaping exception would terminate the
        // process (e.g. replaying a VST param change when the plugin failed to
        // load on this machine). Log and keep going.
        for (auto& fn : actions) {
            try {
                fn();
            } catch (const std::exception& e) {
                std::cerr << "[undo] audio-sync action failed: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[undo] audio-sync action failed: unknown exception" << std::endl;
            }
        }
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
        if (j.contains("version")) {
            const std::vector<int> version = j.at("version").get<std::vector<int>>();
            for (int i : version) {
                if (current->children.empty()) break;
                size_t idx = static_cast<size_t>(i);
                if (idx >= current->children.size())
                    idx = current->children.size() - 1;
                current = current->children[idx];
            }
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

struct SetParamValueUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    std::vector<size_t> paramPath;
    float oldValue = 0.0f;
    float newValue = 0.0f;

    SetParamValueUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath,
                            float oldValue, float newValue, std::string actionName = "Set Param Value");
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

struct MoveMultipleNotesAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = 0;
    std::vector<int> noteIDs;
    std::vector<json> befores;
    std::vector<json> afters;

    MoveMultipleNotesAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID,
                            std::vector<int> noteIDs, std::vector<json> befores, std::vector<json> afters,
                            std::string actionName = "Move Notes");
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
    std::vector<std::pair<int, int>> rhythmVector;
    std::vector<std::pair<int, int>> rhythmEndVector;
    std::vector<std::pair<int, int>> pitchVector;
    /** Filled after PianoRoll::stampNoteTuning on first create; reapplied on redo (doAction) so tuningMode matches the lattice. */
    json noteStampedSnapshot = json();

    CreateNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, std::vector<std::pair<int,int>> startPairs, std::vector<std::pair<int,int>> endPairs,
                     std::vector<std::pair<int, int>> pitchVector);
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

struct CreateAutomationCurveAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int curveID = -1;
    json curveSnapshot = json::object();
    bool snapshotValid = false;

    CreateAutomationCurveAction(Project* p, std::vector<int> managerPath, int nodeID);
    CreateAutomationCurveAction(Project* p, std::vector<int> managerPath, int nodeID, int curveID, json curveSnapshot);
};

struct ModifyCurvePointsAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int curveID = -1;
    json before = json::array();
    json after = json::array();

    ModifyCurvePointsAction(Project* p, std::vector<int> managerPath, int nodeID, int curveID,
                            json before, json after);
};

struct CreateAudioClipAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int clipID = -1;
    json clipSnapshot = json::object();
    bool snapshotValid = false;

    CreateAudioClipAction(Project* p, std::vector<int> managerPath, int nodeID, std::string filepath);
    CreateAudioClipAction(Project* p, std::vector<int> managerPath, int nodeID, int clipID, json clipSnapshot);
};

struct CreatePositionAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int elementID = 0;
    std::vector<std::pair<int, int>> startPairs;
    std::vector<std::pair<int, int>> endPairs;
    std::vector<std::pair<int, int>> startOffsetPairs;
    uint16_t trackID = 0;
    int positionID = -1;
    int rhythmEdoSteps = 1;
    std::vector<std::pair<int, int>> rhythmEdoLower;
    std::vector<std::pair<int, int>> rhythmEdoUpper;

    CreatePositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, std::vector<std::pair<int, int>> startPairs, std::vector<std::pair<int, int>> endPairs, uint16_t trackID, std::vector<std::pair<int, int>> startOffsetPairs = {});
};

struct SongRollRhythmEdoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID;
    json before;
    json after;

    SongRollRhythmEdoAction(Project* p, std::vector<int> managerPath, int nodeID, json before, json after);
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

struct RemoveArrangerTrackAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID;
    int trackType;
    int trackID;
    int connectionID;
    int trackIndex = -1;  // position in the tracks list
    // Snapshot of positions on this track: {elementID, positionIndex, positionJson}
    json positionsSnapshot;
    // Full idManager state snapshots for determinism
    json trackIdPoolSnapshot;
    json connectionIdPoolSnapshot;
    json positionIdPoolSnapshot;
    // Connection list snapshot for the removed connection
    json connectionSnapshot;

    RemoveArrangerTrackAction(Project* p, std::vector<int> managerPath, int nodeID, int trackType, int trackID, int connectionID);
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

struct TogglePianoRollWindowAction : ProjectAction {
    std::vector<int> managerPath;
    int arrangerNodeID;
    int regionID;
    int ewID;
    float x, y, w, h;
    int zOrder;
    bool open;
    TogglePianoRollWindowAction(Project* p, std::vector<int> managerPath, int arrangerNodeID, int regionID,
                                int ewID, float x, float y, float w, float h, int zOrder, bool open);
};

struct ToggleNodeVisibleAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeId = 0;

    ToggleNodeVisibleAction(Project* p, std::vector<int> managerPath, int nodeId);
};

struct PanNodesAction : ProjectAction {
    std::vector<int> managerPath;
    float dx = 0.f, dy = 0.f;
    PanNodesAction(Project* p, std::vector<int> managerPath, float dx, float dy);
};

struct ZoomNodesAction : ProjectAction {
    std::vector<int> managerPath;
    std::vector<float> amounts;
    std::vector<float> mxs;
    std::vector<float> mys;
    std::function<void(float, float, float)> propagateCoalesced;
    ZoomNodesAction(Project* p, std::vector<int> managerPath, float amount, float mx, float my);
    void addStep(float amount, float mx, float my);
};

struct AddNodeAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeType;
    float x;
    float y;
    int nodeID = -1;
    float panOffX = 0.f, panOffY = 0.f;
    /** After undo(removal), redo must restore snapshot (ports, patcher internals, layout) — not a fresh empty node. */
    bool hasRedoRestore = false;
    json redoNodeSnapshot;
    json redoConnectionsSnapshot = json::array();
    /** Multiplexer patcher snapshots for undo/redo idempotency. */
    json patcherData;

    AddNodeAction(Project* p, std::vector<int> managerPath, int type, float x, float y);
};

struct RemoveNodeAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID;
    json nodeData;
    json connectionsData;
    /** Multiplexer patcher snapshots for undo/redo idempotency. */
    json patcherData;
    float panOffX = 0.f, panOffY = 0.f;

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

struct ConnIDs {
    int srcNodeID, srcConID, dstNodeID, dstConID;
    bool existed = false;
};

struct ReassignNodeConnectionAction : ProjectAction {
    std::vector<int> managerPath;
    ConnIDs oldConns[2];
    int oldConnCount = 0;
    int newSrcNodeID, newSrcConID, newDstNodeID, newDstConID;

    ReassignNodeConnectionAction(Project* p, std::vector<int> managerPath,
                                 const ConnIDs* oldCxns, int oldCount,
                                 int nSrcN, int nSrcC, int nDstN, int nDstC);
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

struct AddEQBandAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int bandIndex = 0;
    json bandState;

    AddEQBandAction(Project* p, std::vector<int> managerPath, int nodeID, int bandIndex, json bandState);
};

struct RemoveEQBandAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int bandIndex = 0;
    json bandState;

    RemoveEQBandAction(Project* p, std::vector<int> managerPath, int nodeID, int bandIndex, json bandState);
};

struct VstParameterChangeAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    uint32_t paramID = 0;
    float oldValue = 0;
    float newValue = 0;

    VstParameterChangeAction(Project* p, std::vector<int> managerPath, int nodeID,
                             uint32_t paramID, float oldValue, float newValue);
};

struct VstLoadPluginAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    json oldState;      // previous plugin state (path, bypass, compState, ctrlState)
    json newState;      // new plugin state

    VstLoadPluginAction(Project* p, std::vector<int> managerPath, int nodeID,
                        json oldState, json newState);
};

struct MapParameterUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    size_t paramIndex = 0;
    int vstParamID = -1; // >= 0 means VST parameter
    uint16_t connectionID = 0;
    bool idAssigned = false;

    MapParameterUndoAction(Project* p, std::vector<int> managerPath,
                           int nodeID, size_t paramIndex, int vstParamID = -1);
};

struct UnmapParameterUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    size_t paramIndex = 0;
    int vstParamID = -1;
    uint16_t savedConnectionID = 0;
    bool wasConnected = false;
    uint16_t srcNode = 0;
    uint16_t srcCon = 0;

    UnmapParameterUndoAction(Project* p, std::vector<int> managerPath,
                             int nodeID, size_t paramIndex, int vstParamID = -1);
};

struct ParamNodeAddModRowUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;

    ParamNodeAddModRowUndoAction(Project* p, std::vector<int> managerPath, int nodeID);
};

struct ParamNodeRemoveModRowUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    size_t modIndex = 0;
    float savedDepth = 0.f;
    bool savedCentered = false;
    uint16_t savedConnID = 0;
    bool wasConnected = false;
    uint16_t srcNode = 0;
    uint16_t srcCon = 0;

    ParamNodeRemoveModRowUndoAction(Project* p, std::vector<int> managerPath, int nodeID, size_t modIndex);
};

struct ParamNodeToggleCenteredUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    size_t modIndex = 0;
    bool oldCentered = false;
    bool newCentered = false;
    float oldDepth = 0.f;
    float newDepth = 0.f;

    ParamNodeToggleCenteredUndoAction(Project* p, std::vector<int> managerPath, int nodeID,
                                      size_t modIndex, bool oldCentered, bool newCentered,
                                      float oldDepth, float newDepth);
};
