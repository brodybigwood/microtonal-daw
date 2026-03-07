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

    int size() {
        int size = 1;
        for (auto c : children) size += c->size();
        return size;
    }

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
    static ProjectAction* deSerialize(json);

    ProjectAction(ActionType type) : type(type) {}
};

struct UndoManager {
    ProjectAction* head = new ProjectAction(NullAction);
    ProjectAction* current;

    UndoManager() {
        current = head;
    }

    void newAction(ProjectAction* pa) {
        current->newAction(pa);
        current = pa;
        pa->doAction();
    }

    json serialize() {
        json j;
        j["head"] = ProjectAction::serialize(head);
        j["version"] = current->version();
        return j;
    }

    void deSerialize(json j) {
        if (head) delete head;
        head = ProjectAction::deSerialize(j["head"]);
        std::vector<int> version = j["version"];
        current = head;
        for (auto i : version) {
            current = current->children[i];
        }
    }

    void undo() {
        if (current == head) return;
        current->undoAction();
        current = current->parent;
    }

    void redo() {}
};

struct CreateNoteAction : ProjectAction {
    int noteID;
    int regionID;
    fract start;
    fract length;
    float pitch;
    int scaleID;

    CreateNoteAction(int regionID, fract start, fract length, float pitch, TuningTable* scale) :
        regionID(regionID),
        start(start),
        length(length),
        pitch(pitch),
        scaleID(scale->id),
        ProjectAction(CreateNote)
        {

        doAction = [this] () {
            auto region = static_cast<Region*>(ElementManager::get()->getElement(this->regionID));
            noteID = region->createNote(this->start, this->length, this->pitch, ScaleManager::instance()->byID(this->scaleID));
        };

        undoAction = [this] () {
            auto region = static_cast<Region*>(ElementManager::get()->getElement(this->regionID));
            region->deleteNote(this->noteID);
        };
    }
};
