#pragma once

#include "Region.h"
#include "ElementManager.h"
#include "ScaleManager.h"
#include <mutex>

enum ActionType {
    CreateNote,
    AddArrangerTrack,
    MoveNode,
    AddNode,
    RemoveNode,
    MakeNodeConnection,
    SeverNodeConnection,
    NullAction
};


struct ProjectAction {
    ActionType type;

    std::function<void()> doAction;
    std::function<void()> undoAction;
    bool audioThreadAction = false;

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

    ProjectAction(Project* p, ActionType type) : p(p), type(type) {}

    bool open = false;
    int hoveredIndex = -1;

    std::string name;
    SDL_Texture* texture = nullptr;
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
        head = new ProjectAction(p, NullAction);
        current = head;
    }

    void newAction(ProjectAction* pa) {
        current->newAction(pa);
        current->last_index = pa->index;
        current = pa;
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
        std::vector<int> version = j["version"];
        current = head;
        for (auto i : version) {
            current = current->children[i];
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

    SDL_FRect* baseRect;   
    bool render(SDL_Renderer*);
    bool renderAction(SDL_Renderer*, SDL_FRect*, ProjectAction*);
};

struct CreateNoteAction : ProjectAction {
    int nodeID;
    int noteID;
    int regionID;
    fract start;
    fract length;
    float pitch;
    int scaleID;

    CreateNoteAction(Project* p, int nodeID, int regionID, fract start, fract length, float pitch, TuningTable* scale);
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
