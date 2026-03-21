#pragma once

#include "Region.h"
#include "ElementManager.h"
#include "ScaleManager.h"

enum ActionType {
    CreateNote,
    NullAction
};


struct ProjectAction {
    ActionType type;

    std::function<void()> doAction;
    std::function<void()> undoAction;

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

    UndoManager(Project* p) {
        head = new ProjectAction(p, NullAction);
        current = head;
    }

    void newAction(ProjectAction* pa) {
        current->newAction(pa);
        current->last_index = pa->index;
        current = pa;
        pa->doAction();
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
        current->undoAction();
        current->parent->last_index = current->index;
        current = current->parent;
    }

    void redo(size_t idx = -1) {
        if (idx == -1) idx = current->last_index;
        if (current->children.size()) {
            current = current->children[idx];
            current->doAction();
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
