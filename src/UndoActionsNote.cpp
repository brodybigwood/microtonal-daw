#include "UndoManager.h"
#include "nodes/parametriceq/parametriceq.h"
#include "nodes/vst/vstnode.h"
#include "nodes/arranger/arranger.h"
#include "SongRoll.h"
#include "PianoRollWindow.h"
#include "GridElement.h"
#include <cmath>
#include "SDL_Events.h"
#include "styles.h"
#include <functional>
#include "Project.h"
#include "NodeProcessor.h"
#include "NodeEditor.h"
#include "nodes/nodetypes.h"
#include "NodeManager.h"
#include "InputNode.h"
#include "OutputNode.h"
#include "Note.h"
#include "PianoRoll.h"
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "UndoInternal.h"

// Note actions: create/move/delete, tuning, harmonic assignment (split from UndoManager.cpp).

CreateNoteAction::CreateNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, std::vector<std::pair<int,int>> startPairs,
                                   std::vector<std::pair<int,int>> endPairs, std::vector<std::pair<int, int>> pitchVector) :
        ProjectAction(p, CreateNote),
        managerPath(std::move(managerPath)),
        regionID(regionID),
        rhythmVector(std::move(startPairs)),
        rhythmEndVector(std::move(endPairs)),
        pitchVector(std::move(pitchVector)),
        nodeID(nodeID) {
    doAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        noteID = region.createNote(this->rhythmVector, this->rhythmEndVector, this->pitchVector);
        name = "Create Note " + std::to_string(noteID) + " " + std::to_string(this->regionID);
        if (!noteStampedSnapshot.is_null()) {
            auto it = region.id_to_index.find(noteID);
            if (it != region.id_to_index.end()) {
                const size_t idx = static_cast<size_t>(it->second);
                if (idx < region.notes.size() && region.notes[idx])
                    region.notes[idx]->applyUndoSnapshot(noteStampedSnapshot);
            }
        }
    };

    undoAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        region.deleteNote(this->noteID);
    };
}

MoveNoteAction::MoveNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID, json before,
                               json after, std::string actionName) :
        ProjectAction(p, MoveNote),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteID(noteID),
        before(std::move(before)),
        after(std::move(after)) {
    skipInitialDo = true;
    name = std::move(actionName);
    doAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        auto it = region.id_to_index.find(this->noteID);
        if (it == region.id_to_index.end())
            throw std::runtime_error("MoveNoteAction::doAction: note id not in region");
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= region.notes.size())
            throw std::runtime_error("MoveNoteAction::doAction: note index out of range");
        region.notes[idx]->applyUndoSnapshot(this->after);
    };
    undoAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        auto it = region.id_to_index.find(this->noteID);
        if (it == region.id_to_index.end())
            throw std::runtime_error("MoveNoteAction::undoAction: note id not in region");
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= region.notes.size())
            throw std::runtime_error("MoveNoteAction::undoAction: note index out of range");
        region.notes[idx]->applyUndoSnapshot(this->before);
    };
}

MoveMultipleNotesAction::MoveMultipleNotesAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID,
                                                  std::vector<int> noteIDs, std::vector<json> befores, std::vector<json> afters,
                                                  std::string actionName) :
        ProjectAction(p, MoveMultipleNotes),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteIDs(std::move(noteIDs)),
        befores(std::move(befores)),
        afters(std::move(afters)) {
    skipInitialDo = true;
    name = std::move(actionName);
    doAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        for (size_t i = 0; i < this->noteIDs.size(); ++i) {
            auto it = region.id_to_index.find(this->noteIDs[i]);
            if (it == region.id_to_index.end()) continue;
            const size_t idx = static_cast<size_t>(it->second);
            if (idx >= region.notes.size() || !region.notes[idx]) continue;
            region.notes[idx]->applyUndoSnapshot(this->afters[i]);
        }
    };
    undoAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        for (size_t i = 0; i < this->noteIDs.size(); ++i) {
            auto it = region.id_to_index.find(this->noteIDs[i]);
            if (it == region.id_to_index.end()) continue;
            const size_t idx = static_cast<size_t>(it->second);
            if (idx >= region.notes.size() || !region.notes[idx]) continue;
            region.notes[idx]->applyUndoSnapshot(this->befores[i]);
        }
    };
}

void DeleteNoteAction::wireDeleteLambdas() {
    doAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.deleteNote(this->noteID);
    };
    undoAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        json snap = this->noteSnapshot;
        std::shared_ptr<Note> n = Note::fromJSON(snap);
        r.restoreNoteAt(std::move(n), this->insertIndex);
    };
}

DeleteNoteAction::DeleteNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID) :
        ProjectAction(p, DeleteNote),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteID(noteID) {
    name = "Delete Note";
    Region& region = *undoResolveArrangerRegion(p, this->managerPath, nodeID, regionID);
    auto it = region.id_to_index.find(noteID);
    if (it == region.id_to_index.end())
        throw std::runtime_error("DeleteNoteAction: note id not in region");
    insertIndex = static_cast<size_t>(it->second);
    if (insertIndex >= region.notes.size() || !region.notes[insertIndex] || region.notes[insertIndex]->id != noteID)
        throw std::runtime_error("DeleteNoteAction: note index or id mismatch");
    noteSnapshot = region.notes[insertIndex]->toJSON();
    wireDeleteLambdas();
}

DeleteNoteAction::DeleteNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID,
                                   size_t insertIndex, json noteSnapshot) :
        ProjectAction(p, DeleteNote),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteID(noteID),
        insertIndex(insertIndex),
        noteSnapshot(std::move(noteSnapshot)) {
    name = "Delete Note";
    wireDeleteLambdas();
}

PianoRollRegionTuningUndoAction::PianoRollRegionTuningUndoAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID,
                                                                 json beforeRegion, json afterRegion, std::string actionName) :
        ProjectAction(p, PianoRollRegionTuning),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        beforeRegion(std::move(beforeRegion)),
        afterRegion(std::move(afterRegion)) {
    if (actionName.empty())
        throw std::runtime_error("PianoRollRegionTuningUndoAction: actionName must be non-empty");
    name = std::move(actionName);
    doAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.applyTuningUndoFromJSON(this->afterRegion);
        PianoRoll::notifyTuningUndoApplied(this->p, this->managerPath, this->nodeID, this->regionID, -1);
    };
    undoAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.applyTuningUndoFromJSON(this->beforeRegion);
        PianoRoll::notifyTuningUndoApplied(this->p, this->managerPath, this->nodeID, this->regionID, -1);
    };
}

AssignNoteHarmonicUndoAction::AssignNoteHarmonicUndoAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID,
                                                           int noteID, json beforeRegion, json afterRegion, json beforeNote,
                                                           json afterNote) :
        ProjectAction(p, AssignNoteHarmonic),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteID(noteID),
        beforeRegion(std::move(beforeRegion)),
        afterRegion(std::move(afterRegion)),
        beforeNote(std::move(beforeNote)),
        afterNote(std::move(afterNote)) {
    name = "Assign Note Harmonic";
    doAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.applyTuningUndoFromJSON(this->afterRegion);
        auto it = r.id_to_index.find(this->noteID);
        if (it == r.id_to_index.end())
            throw std::runtime_error("AssignNoteHarmonicUndoAction::doAction: note id not in region");
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= r.notes.size())
            throw std::runtime_error("AssignNoteHarmonicUndoAction::doAction: note index out of range");
        const std::shared_ptr<Note>& note = r.notes[idx];
        if (!note)
            throw std::runtime_error("AssignNoteHarmonicUndoAction::doAction: null note");
        note->applyTuningFieldsUndoFromJSON(this->afterNote);
        PianoRoll::notifyTuningUndoApplied(this->p, this->managerPath, this->nodeID, this->regionID, this->noteID);
    };
    undoAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.applyTuningUndoFromJSON(this->beforeRegion);
        auto it = r.id_to_index.find(this->noteID);
        if (it == r.id_to_index.end())
            throw std::runtime_error("AssignNoteHarmonicUndoAction::undoAction: note id not in region");
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= r.notes.size())
            throw std::runtime_error("AssignNoteHarmonicUndoAction::undoAction: note index out of range");
        const std::shared_ptr<Note>& note = r.notes[idx];
        if (!note)
            throw std::runtime_error("AssignNoteHarmonicUndoAction::undoAction: null note");
        note->applyTuningFieldsUndoFromJSON(this->beforeNote);
        PianoRoll::notifyTuningUndoApplied(this->p, this->managerPath, this->nodeID, this->regionID, this->noteID);
    };
}

