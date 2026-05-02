#pragma once

#include "Region.h"
#include "ElementManager.h"
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
    CreateRegion = 14
};


struct ProjectAction {
    ActionType type;

    std::function<void()> doAction;
    std::function<void()> undoAction;
    bool audioThreadAction = false;
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
};

struct UndoHeadAction : ProjectAction {
    explicit UndoHeadAction(Project* p) : ProjectAction(p, UndoHead) {}
};

struct UndoManager {
    ProjectAction* head;
    ProjectAction* current;

    std::mutex audioActionMutex;
    std::vector<std::function<void()>> pendingAudioActions;

    void enqueueAudioAction(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(audioActionMutex);
        pendingAudioActions.push_back(std::move(fn));
    }

    void flushAudioActions() {
        std::vector<std::function<void()>> actions;
        {
            std::lock_guard<std::mutex> lock(audioActionMutex);
            actions.swap(pendingAudioActions);
        }
        for (auto& fn : actions) fn();
    }

    UndoManager(Project* p) {
        head = new UndoHeadAction(p);
        current = head;
    }

    void newAction(ProjectAction* pa) {
        current->newAction(pa);
        current->last_index = pa->index;
        current = pa;
        if (pa->skipInitialDo)
            return;
        if (pa->audioThreadAction) enqueueAudioAction(pa->doAction);
        else pa->doAction();
    }

    json serialize() {
        json j;
        j["head"] = ProjectAction::serialize(head);
        j["version"] = current->version();
        return j;
    }

    void deSerialize(json j, Project* p) {
        if (head) delete head;
        head = ProjectAction::deSerialize(j["head"], p);
        current = head;
        const std::vector<int> version = j.at("version").get<std::vector<int>>();
        for (int i : version) {
            current = current->children[static_cast<size_t>(i)];
        }
    }

    void undo() {
        if (current == head) return;
        if (current->audioThreadAction) enqueueAudioAction(current->undoAction);
        else current->undoAction();
        current->parent->last_index = current->index;
        current = current->parent;
    }

    void redo(size_t idx = -1) {
        if (idx == -1) idx = current->last_index;
        if (current->children.size()) {
            current = current->children[idx];
            if (current->audioThreadAction) enqueueAudioAction(current->doAction);
            else current->doAction();
        }
    }

    void goTo(ProjectAction*);
    bool clicked = false;

    /** When non-null, hit-testing for the undo tree uses client coords of this window instead of `MouseOn`. */
    SDL_Window* hitTestWindow = nullptr;

    SDL_FRect* baseRect = nullptr;
    bool mouseHitsRect(SDL_FRect* rect) const;
    void clearAllRenderTextures();
    bool render(SDL_Renderer*);
    bool renderAction(SDL_Renderer*, SDL_FRect*, ProjectAction*);

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
    size_t paramIndex = 0;

    AddModSourceUndoAction(Project* p, std::vector<int> managerPath, int nodeID, size_t paramIndex);
};

struct RemoveModSourceUndoAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    size_t paramIndex = 0;
    size_t modIndex = 0;

    RemoveModSourceUndoAction(Project* p, std::vector<int> managerPath, int nodeID, size_t paramIndex, size_t modIndex);
};

struct MoveNoteAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = 0;
    int noteID = 0;
    json before;
    json after;

    MoveNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID, json before,
                   json after);
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

    CreateNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, fract start, fract length,
                     float pitch, std::vector<std::pair<int, int>> pitchIntegerPairs);
};

struct CreateRegionAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID = 0;
    int regionID = -1;
    fract start;
    uint16_t trackID = 0;
    json regionSnapshot = json::object();
    bool snapshotValid = false;

    CreateRegionAction(Project* p, std::vector<int> managerPath, int nodeID, fract start, uint16_t trackID);
    /** Restored from project / undo JSON (`skipInitialDo`, graph already matches "after" for current pointer). */
    CreateRegionAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, json regionSnapshot);
};

struct AddArrangerTrackAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID;
    int trackType;
    int trackID = -1;
    int connectionID = -1;

    AddArrangerTrackAction(Project* p, std::vector<int> managerPath, int nodeID, int trackType);
};

struct MoveNodeAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeID;
    float fromX;
    float fromY;
    float toX;
    float toY;

    MoveNodeAction(Project* p, std::vector<int> managerPath, int nodeID, float fromX, float fromY, float toX, float toY);
};

struct AddNodeAction : ProjectAction {
    std::vector<int> managerPath;
    int nodeType;
    float x;
    float y;
    int nodeID = -1;

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
