#include "UndoManager.h"
#include "nodes/vst/vstnode.h"
#include "nodes/arranger/arranger.h"
#include "SongRoll.h"
#include "PianoRollWindow.h"
#include "PianoRollInternal.h"
#include "GridElement.h"
#include "AutomationCurve.h"
#include "AudioClip.h"
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
#include <map>
#include <set>
#include <limits>
#include <vector>
#include "UndoInternal.h"

// Region, timeline-position and arranger-track actions (split from UndoManager.cpp).

static void recalcTrackChannels(ElementManager* em, uint16_t trackID) {
    (void)trackID;
    if (!em || !em->parentNode) return;
    // Track width follows both clips and the connected input source.
    em->parentNode->syncWidthsFromInputs();
    em->parentNode->nm->markTopologyDirty();
}

static GridElement* undoResolveGridElement(Project* p, const std::vector<int>& managerPath, int nodeID, int elementID) {
    ElementManager* em = undoResolveArrangerElementManager(p, managerPath, nodeID);
    if (!em)
        throw std::runtime_error("undoResolveGridElement: element manager missing");
    return em->getElement(static_cast<uint16_t>(elementID));
}

static GridElement::Position* undoResolveElementPosition(Project* p, const std::vector<int>& managerPath, int nodeID, int elementID,
                                                         int positionID) {
    GridElement* ge = undoResolveGridElement(p, managerPath, nodeID, elementID);
    for (auto* pos : ge->positions) {
        if (pos->id == positionID)
            return pos;
    }
    throw std::runtime_error("undoResolveElementPosition: position id not found");
}

namespace {
void wireCreateRegionDoUndo(CreateRegionAction* t) {
    t->doAction = [t]() {
        ElementManager* em = undoResolveArrangerElementManager(t->p, t->managerPath, t->nodeID);
        if (!em)
            throw std::runtime_error("CreateRegionAction::doAction: element manager missing");
        if (!t->snapshotValid) {
            Region* r = em->newRegion();
            t->regionID = static_cast<int>(r->id);
            t->regionSnapshot = r->toJSON();
            t->snapshotValid = true;
            t->name = "Create Region " + std::to_string(t->regionID);
        } else {
            em->restoreRegionFromSnapshot(t->regionSnapshot);
        }
    };
    t->undoAction = [t]() {
        if (!t->snapshotValid)
            return;
        ElementManager* em = undoResolveArrangerElementManager(t->p, t->managerPath, t->nodeID);
        if (!em)
            throw std::runtime_error("CreateRegionAction::undoAction: element manager missing");
        ArrangerNode* arr = undoResolveArrangerNode(t->p, t->managerPath, t->nodeID);
        if (arr && arr->sl)
            arr->sl->clearPianoRoll(t->regionID, false);
        em->removeElementById(static_cast<uint16_t>(t->regionID));
    };
}
} // namespace

CreateRegionAction::CreateRegionAction(Project* p, std::vector<int> managerPath, int nodeID) :
        ProjectAction(p, CreateRegion),
        managerPath(std::move(managerPath)),
        nodeID(nodeID) {
    wireCreateRegionDoUndo(this);
}

CreateRegionAction::CreateRegionAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, json regionSnapshot) :
        ProjectAction(p, CreateRegion),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        regionSnapshot(std::move(regionSnapshot)),
        snapshotValid(true) {
    skipInitialDo = true;
    name = "Create Region " + std::to_string(this->regionID);
    wireCreateRegionDoUndo(this);
}

// ---------------------------------------------------------------------------
// CreateAutomationCurveAction
// ---------------------------------------------------------------------------
namespace {
void wireCreateAutomationCurveDoUndo(CreateAutomationCurveAction* t) {
    t->doAction = [t]() {
        ElementManager* em = undoResolveArrangerElementManager(t->p, t->managerPath, t->nodeID);
        if (!em)
            throw std::runtime_error("CreateAutomationCurveAction::doAction: element manager missing");
        if (!t->snapshotValid) {
            AutomationCurve* c = em->newAutomationCurve();
            t->curveID = static_cast<int>(c->id);
            t->curveSnapshot = c->toJSON();
            t->snapshotValid = true;
            t->name = "Create Automation Curve " + std::to_string(t->curveID);
        } else {
            em->restoreAutomationCurveFromSnapshot(t->curveSnapshot);
        }
    };
    t->undoAction = [t]() {
        if (!t->snapshotValid)
            return;
        ElementManager* em = undoResolveArrangerElementManager(t->p, t->managerPath, t->nodeID);
        if (!em)
            throw std::runtime_error("CreateAutomationCurveAction::undoAction: element manager missing");
        em->removeElementById(static_cast<uint16_t>(t->curveID));
    };
}
} // namespace

CreateAutomationCurveAction::CreateAutomationCurveAction(Project* p, std::vector<int> managerPath, int nodeID) :
        ProjectAction(p, CreateAutomationCurve),
        managerPath(std::move(managerPath)),
        nodeID(nodeID) {
    wireCreateAutomationCurveDoUndo(this);
}

CreateAutomationCurveAction::CreateAutomationCurveAction(Project* p, std::vector<int> managerPath, int nodeID, int curveID, json curveSnapshot) :
        ProjectAction(p, CreateAutomationCurve),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        curveID(curveID),
        curveSnapshot(std::move(curveSnapshot)),
        snapshotValid(true) {
    skipInitialDo = true;
    name = "Create Automation Curve " + std::to_string(this->curveID);
    wireCreateAutomationCurveDoUndo(this);
}

// ---------------------------------------------------------------------------
// CreateAudioClipAction
// ---------------------------------------------------------------------------
namespace {
void wireCreateAudioClipDoUndo(CreateAudioClipAction* t) {
    t->doAction = [t]() {
        ElementManager* em = undoResolveArrangerElementManager(t->p, t->managerPath, t->nodeID);
        if (!em)
            throw std::runtime_error("CreateAudioClipAction::doAction: element manager missing");
        if (!t->snapshotValid) {
            AudioClip* c = em->newAudioClip(t->clipSnapshot["filepath"]);
            if (!c)
                throw std::runtime_error("CreateAudioClipAction::doAction: failed to load audio file");
            t->clipID = static_cast<int>(c->id);
            t->clipSnapshot = c->toJSON();
            t->snapshotValid = true;
            t->name = "Create Audio Clip " + std::to_string(t->clipID);
        } else {
            em->restoreAudioClipFromSnapshot(t->clipSnapshot);
        }
        em->parentNode->syncWidthsFromInputs();
        em->parentNode->nm->markTopologyDirty();
    };
    t->undoAction = [t]() {
        if (!t->snapshotValid)
            return;
        ElementManager* em = undoResolveArrangerElementManager(t->p, t->managerPath, t->nodeID);
        if (!em)
            throw std::runtime_error("CreateAudioClipAction::undoAction: element manager missing");
        em->removeElementById(static_cast<uint16_t>(t->clipID));
        em->parentNode->syncWidthsFromInputs();
        em->parentNode->nm->markTopologyDirty();
    };
}
} // namespace

CreateAudioClipAction::CreateAudioClipAction(Project* p, std::vector<int> managerPath, int nodeID, std::string filepath) :
        ProjectAction(p, CreateAudioClip),
        managerPath(std::move(managerPath)),
        nodeID(nodeID) {
    clipSnapshot["filepath"] = std::move(filepath);
    wireCreateAudioClipDoUndo(this);
}

CreateAudioClipAction::CreateAudioClipAction(Project* p, std::vector<int> managerPath, int nodeID, int clipID, json clipSnapshot) :
        ProjectAction(p, CreateAudioClip),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        clipID(clipID),
        clipSnapshot(std::move(clipSnapshot)),
        snapshotValid(true) {
    skipInitialDo = true;
    name = "Create Audio Clip " + std::to_string(this->clipID);
    wireCreateAudioClipDoUndo(this);
}

// ---------------------------------------------------------------------------
// ModifyCurvePointsAction
// ---------------------------------------------------------------------------
ModifyCurvePointsAction::ModifyCurvePointsAction(Project* p, std::vector<int> managerPath, int nodeID, int curveID,
                                                 json before, json after) :
        ProjectAction(p, ModifyCurvePoints),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        curveID(curveID),
        before(std::move(before)),
        after(std::move(after)) {
    auto curveJsonToPoints = [](const json& pts) -> std::string {
        std::string s;
        for (const auto& pt : pts) {
            if (!s.empty()) s += ",";
            s += std::to_string(static_cast<int>(pt.value("v", 0.f) * 100.f));
        }
        return s;
    };
    name = "Edit Curve " + std::to_string(curveID) + " (" + curveJsonToPoints(before) + " -> " + curveJsonToPoints(after) + ")";
    doAction = [this]() {
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (!em) return;
        GridElement* ge = em->getElement(static_cast<uint16_t>(this->curveID));
        if (!ge || ge->type != ElementType::automationCurve) return;
        auto* ac = static_cast<AutomationCurve*>(ge);
        auto j = ac->toJSON();
        if (this->after.is_array()) {
            ac->points.clear();
            for (const auto& el : this->after) {
                CurvePoint pt;
                pt.v = el.value("v", 0.f);
                pt.shape.type = static_cast<CurveShape::Type>(el.value("shape", static_cast<int>(CurveShape::Single)));
                pt.shape.param = el.value("shapeParam", 0.f);
                if (el.contains("timeVec") && el["timeVec"].is_array())
                    for (const auto& pr : el["timeVec"])
                        if (pr.is_array() && pr.size() >= 2)
                            pt.timeVec.push_back({pr[0].get<int>(), pr[1].get<int>()});
                ac->points.push_back(std::move(pt));
            }
        }
    };
    undoAction = [this]() {
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (!em) return;
        GridElement* ge = em->getElement(static_cast<uint16_t>(this->curveID));
        if (!ge || ge->type != ElementType::automationCurve) return;
        auto* ac = static_cast<AutomationCurve*>(ge);
        if (this->before.is_array()) {
            ac->points.clear();
            for (const auto& el : this->before) {
                CurvePoint pt;
                pt.v = el.value("v", 0.f);
                pt.shape.type = static_cast<CurveShape::Type>(el.value("shape", static_cast<int>(CurveShape::Single)));
                pt.shape.param = el.value("shapeParam", 0.f);
                if (el.contains("timeVec") && el["timeVec"].is_array())
                    for (const auto& pr : el["timeVec"])
                        if (pr.is_array() && pr.size() >= 2)
                            pt.timeVec.push_back({pr[0].get<int>(), pr[1].get<int>()});
                ac->points.push_back(std::move(pt));
            }
        }
    };
}

void DeleteRegionAction::wireDeleteRegionLambdas() {
    doAction = [this]() {
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (!em)
            throw std::runtime_error("DeleteRegionAction::doAction: element manager missing");
        ArrangerNode* arr = undoResolveArrangerNode(this->p, this->managerPath, this->nodeID);
        if (arr && arr->sl)
            arr->sl->clearPianoRoll(this->regionID, false);
        em->removeElementById(static_cast<uint16_t>(this->regionID));
    };
    undoAction = [this]() {
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (!em)
            throw std::runtime_error("DeleteRegionAction::undoAction: element manager missing");
        em->restoreRegionFromSnapshotAt(this->elementInsertIndex, this->regionSnapshot);
    };
}

DeleteRegionAction::DeleteRegionAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID) :
        ProjectAction(p, DeleteRegion),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID) {
    ElementManager* em = undoResolveArrangerElementManager(p, this->managerPath, nodeID);
    if (!em)
        throw std::runtime_error("DeleteRegionAction: element manager missing");
    const uint16_t rid = static_cast<uint16_t>(regionID);
    const auto idxIt = em->ids.find(rid);
    if (idxIt == em->ids.end())
        throw std::runtime_error("DeleteRegionAction: region id not in element manager");
    const size_t idx = static_cast<size_t>(idxIt->second);
    if (idx >= em->elements.size())
        throw std::runtime_error("DeleteRegionAction: index out of range");
    GridElement* ge = em->elements[idx];
    if (ge->type != ElementType::region || ge->id != rid)
        throw std::runtime_error("DeleteRegionAction: not a region or id mismatch");
    regionSnapshot = static_cast<Region*>(ge)->toJSON();
    elementInsertIndex = idx;
    name = "Delete Region " + std::to_string(regionID);
    wireDeleteRegionLambdas();
}

DeleteRegionAction::DeleteRegionAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, size_t elementInsertIndex,
                                       json regionSnapshot) :
        ProjectAction(p, DeleteRegion),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        elementInsertIndex(elementInsertIndex),
        regionSnapshot(std::move(regionSnapshot)) {
    skipInitialDo = true;
    name = "Delete Region " + std::to_string(regionID);
    wireDeleteRegionLambdas();
}

CreatePositionAction::CreatePositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, std::vector<std::pair<int, int>> startPairs, std::vector<std::pair<int, int>> endPairs, uint16_t trackID, std::vector<std::pair<int, int>> startOffsetPairs) :
        ProjectAction(p, CreatePosition),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        elementID(elementID),
        startPairs(std::move(startPairs)),
        endPairs(std::move(endPairs)),
        trackID(trackID),
        startOffsetPairs(std::move(startOffsetPairs)) {
    // The arranger may not exist yet: Project::load deserializes the undo tree
    // before restoring the graph. Missing it just means "keep the defaults";
    // the constructor must not throw or the project cannot be loaded.
    NodeManager& nm = requireManager(p, this->managerPath);
    ArrangerNode* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
    if (arr) {
        rhythmEdoSteps = arr->rhythmEdoSubdivisionSteps;
        rhythmEdoLower = arr->rhythmEdoLowerVector;
        rhythmEdoUpper = arr->rhythmEdoUpperVector;
    }
    doAction = [this]() {
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        ge->createPos(this->startPairs, this->endPairs, this->trackID,
                      this->rhythmEdoSteps, this->rhythmEdoLower, this->rhythmEdoUpper,
                      this->startOffsetPairs);
        this->positionID = ge->positions.back()->id;
        this->name = "Create Position " + std::to_string(this->positionID);
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (em) recalcTrackChannels(em, this->trackID);
    };
    undoAction = [this]() {
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        if (!ge->removePositionById(this->positionID))
            throw std::runtime_error("CreatePositionAction::undoAction: position id missing");
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (em) recalcTrackChannels(em, this->trackID);
    };
}

SongRollRhythmEdoAction::SongRollRhythmEdoAction(Project* p, std::vector<int> managerPath, int nodeID, json before, json after) :
        ProjectAction(p, SongRollRhythmEdo),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        before(std::move(before)),
        after(std::move(after)) {
    skipInitialDo = true;
    name = "SongRoll Rhythm EDO";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!node) return;
        node->rhythmEdoSubdivisionSteps = this->after["steps"].get<int>();
        node->rhythmEdoLowerVector.clear();
        for (const auto& el : this->after["lower"])
            if (el.is_array() && el.size() >= 2)
                node->rhythmEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
        node->rhythmEdoUpperVector.clear();
        for (const auto& el : this->after["upper"])
            if (el.is_array() && el.size() >= 2)
                node->rhythmEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
        node->transportLines = latticeSpansFromJSON(this->after, "transportLines");
        node->gridSpans = latticeSpansFromJSON(this->after, "gridSpans");
        if (node->gridSpans.empty()) node->gridSpans.push_back(defaultLatticeSpan(1));
        if (node->sl) node->sl->updateRhythmLines();
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!node) return;
        node->rhythmEdoSubdivisionSteps = this->before["steps"].get<int>();
        node->rhythmEdoLowerVector.clear();
        for (const auto& el : this->before["lower"])
            if (el.is_array() && el.size() >= 2)
                node->rhythmEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
        node->rhythmEdoUpperVector.clear();
        for (const auto& el : this->before["upper"])
            if (el.is_array() && el.size() >= 2)
                node->rhythmEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
        node->transportLines = latticeSpansFromJSON(this->before, "transportLines");
        node->gridSpans = latticeSpansFromJSON(this->before, "gridSpans");
        if (node->gridSpans.empty()) node->gridSpans.push_back(defaultLatticeSpan(1));
        if (node->sl) node->sl->updateRhythmLines();
    };
}

void DeletePositionAction::wireLambdas() {
    doAction = [this]() {
        uint16_t tid = static_cast<uint16_t>(this->positionSnapshot.value("trackID", 0));
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        if (!ge->removePositionById(this->positionID))
            throw std::runtime_error("DeletePositionAction::doAction: position id missing");
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (em) recalcTrackChannels(em, tid);
    };
    undoAction = [this]() {
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        ge->insertPositionAt(this->insertIndex, this->positionSnapshot);
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (em) recalcTrackChannels(em, static_cast<uint16_t>(this->positionSnapshot.value("trackID", 0)));
    };
}

DeletePositionAction::DeletePositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, int positionID) :
        ProjectAction(p, DeletePosition),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        elementID(elementID),
        positionID(positionID) {
    GridElement* ge = undoResolveGridElement(p, this->managerPath, nodeID, elementID);
    bool found = false;
    for (size_t i = 0; i < ge->positions.size(); ++i) {
        if (ge->positions[i]->id == positionID) {
            insertIndex = i;
            positionSnapshot = GridElement::positionToJson(*ge->positions[i]);
            found = true;
            break;
        }
    }
    if (!found)
        throw std::runtime_error("DeletePositionAction: position id not found on element");
    name = "Delete Position " + std::to_string(positionID);
    wireLambdas();
}

DeletePositionAction::DeletePositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, int positionID,
                                           size_t insertIndex, json positionSnapshot) :
        ProjectAction(p, DeletePosition),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        elementID(elementID),
        positionID(positionID),
        insertIndex(insertIndex),
        positionSnapshot(std::move(positionSnapshot)) {
    skipInitialDo = true;
    name = "Delete Position " + std::to_string(positionID);
    wireLambdas();
}

MoveElementPositionAction::MoveElementPositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, int positionID,
                                                     json before, json after) :
        ProjectAction(p, MoveElementPosition),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        elementID(elementID),
        positionID(positionID),
        before(std::move(before)),
        after(std::move(after)) {
    skipInitialDo = true;
    name = "Move Position";
    doAction = [this]() {
        GridElement::Position* pos = undoResolveElementPosition(this->p, this->managerPath, this->nodeID, this->elementID, this->positionID);
        uint16_t oldTid = static_cast<uint16_t>(this->before.value("trackID", 0));
        GridElement::applyPositionFromJson(pos, this->after);
        uint16_t newTid = static_cast<uint16_t>(this->after.value("trackID", 0));
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (em) {
            if (oldTid != newTid) recalcTrackChannels(em, oldTid);
            recalcTrackChannels(em, newTid);
        }
    };
    undoAction = [this]() {
        GridElement::Position* pos = undoResolveElementPosition(this->p, this->managerPath, this->nodeID, this->elementID, this->positionID);
        uint16_t newTid = static_cast<uint16_t>(this->after.value("trackID", 0));
        GridElement::applyPositionFromJson(pos, this->before);
        uint16_t oldTid = static_cast<uint16_t>(this->before.value("trackID", 0));
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (em) {
            if (oldTid != newTid) recalcTrackChannels(em, newTid);
            recalcTrackChannels(em, oldTid);
        }
    };
}

AddArrangerTrackAction::AddArrangerTrackAction(Project* p, std::vector<int> managerPath, int nodeID, int trackType) :
        ProjectAction(p, AddArrangerTrack),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        trackType(trackType) {
    name = "Add Arranger Track";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!node)
            throw std::runtime_error("AddArrangerTrackAction::doAction: node is not an arranger");
        TrackManager* tm = node->tracks;
        if (!tm)
            throw std::runtime_error("AddArrangerTrackAction::doAction: no active track manager");
        auto* track =
            tm->addTrackNow(static_cast<TrackType>(this->trackType), this->trackID, this->connectionID,
                            -1, this->inputConnectionID);
        if (!track)
            throw std::runtime_error("AddArrangerTrackAction::doAction: addTrackNow failed");
        this->trackID = track->id;
        if (track->connection)
            this->connectionID = track->connection->id;
        if (track->inputConnection)
            this->inputConnectionID = track->inputConnection->id;
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!node)
            throw std::runtime_error("AddArrangerTrackAction::undoAction: node is not an arranger");
        if (this->trackID < 0)
            throw std::runtime_error("AddArrangerTrackAction::undoAction: invalid trackID");
        TrackManager* tm = node->tracks;
        if (!tm)
            throw std::runtime_error("AddArrangerTrackAction::undoAction: no active track manager");
        tm->removeTrackNow(static_cast<uint16_t>(this->trackID));
    };
}

RemoveArrangerTrackAction::RemoveArrangerTrackAction(Project* p, std::vector<int> managerPath, int nodeID, int trackType, int trackID, int connectionID) :
        ProjectAction(p, RemoveArrangerTrack),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        trackType(trackType),
        trackID(trackID),
        connectionID(connectionID) {
    name = "Remove Arranger Track";

    // Snapshot positions on this track before deletion.
    NodeManager& nm = requireManager(p, this->managerPath);
    auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
    if (node && node->tracks) {
        trackIndex = node->tracks->getIndex(static_cast<uint16_t>(this->trackID));
        // Save full idManager state for track and connection pools.
        trackIdPoolSnapshot = node->tracks->getIdPool().toJSON();
        connectionIdPoolSnapshot = node->outputs.id_pool.toJSON();
        Track* tr = node->tracks->getTrack(static_cast<uint16_t>(this->trackID));
        if (tr && tr->inputConnection) {
            inputConnectionID = tr->inputConnection->id;
            if (tr->inputConnection->is_connected) {
                inputWasConnected = true;
                inputSourceNodeID = tr->inputConnection->input_node;
                inputSourceConnectionID = tr->inputConnection->input_connection;
            }
        }
    }
    if (node && node->elements) {
        positionIdPoolSnapshot = node->elements->id_pool.toJSON();
        positionsSnapshot = json::array();
        for (auto* el : node->elements->elements) {
            for (size_t i = 0; i < el->positions.size(); ++i) {
                if (el->positions[i]->trackID == static_cast<uint16_t>(this->trackID)) {
                    json entry;
                    entry["elementID"] = static_cast<int>(el->id);
                    entry["index"] = static_cast<int>(i);
                    entry["pos"] = GridElement::positionToJson(*el->positions[i]);
                    positionsSnapshot.push_back(entry);
                }
            }
        }
    }

    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!node || !node->elements) return;
        // Remove positions on this track (reverse order to preserve indices).
        for (int i = static_cast<int>(positionsSnapshot.size()) - 1; i >= 0; --i) {
            auto& entry = positionsSnapshot[static_cast<size_t>(i)];
            int elemID = entry["elementID"].get<int>();
            int posID = entry["pos"]["id"].get<int>();
            auto* el = node->elements->getElement(static_cast<uint16_t>(elemID));
            if (el) el->removePositionById(posID);
        }
        if (node->tracks)
            node->tracks->removeTrackNow(static_cast<uint16_t>(this->trackID));
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!node || !node->tracks) return;
        node->tracks->addTrackNow(static_cast<TrackType>(this->trackType), this->trackID, this->connectionID,
                                  this->trackIndex, this->inputConnectionID);
        if (this->inputWasConnected && this->inputConnectionID >= 0)
            node->nm->makeNodeConnectionNow(static_cast<uint16_t>(this->inputSourceNodeID),
                                            static_cast<uint16_t>(this->inputSourceConnectionID),
                                            static_cast<uint16_t>(this->nodeID),
                                            static_cast<uint16_t>(this->inputConnectionID));
        // Restore idManager state for track and connection pools.
        if (!trackIdPoolSnapshot.is_null())
            node->tracks->getIdPool().fromJSON(trackIdPoolSnapshot);
        if (!connectionIdPoolSnapshot.is_null())
            node->outputs.id_pool.fromJSON(connectionIdPoolSnapshot);
        // Restore positions.
        if (node->elements) {
            for (auto& entry : positionsSnapshot) {
                int elemID = entry["elementID"].get<int>();
                int index = entry["index"].get<int>();
                auto* el = node->elements->getElement(static_cast<uint16_t>(elemID));
                if (el) el->insertPositionAt(static_cast<size_t>(index), entry["pos"]);
            }
            // Restore position idManager state.
            if (!positionIdPoolSnapshot.is_null())
                node->elements->position_id_pool.fromJSON(positionIdPoolSnapshot);
        }
    };
}

// ---------------------------------------------------------------------------
// ArrangerTrackControlAction
// ---------------------------------------------------------------------------
namespace {
bool arrangerTrackControlValue(TrackManager* tm, uint16_t trackID, int control) {
    if (!tm) return false;
    switch (control) {
        case 0: return tm->isSoloed(trackID);
        case 1: return tm->isMuted(trackID);
        case 2: { Track* t = tm->getTrack(trackID); return t && t->recordArmed; }
        case 3: { Track* t = tm->getTrack(trackID); return t && t->monitor; }
        case 4: { Track* t = tm->getTrack(trackID); return t && t->stereoMono; }
        default: return false;
    }
}

void setArrangerTrackControlValue(TrackManager* tm, uint16_t trackID, int control, bool on) {
    if (!tm) return;
    switch (control) {
        case 0: tm->setSoloState(trackID, on); break;
        case 1: tm->setMuteState(trackID, on); break;
        case 2: tm->setRecordArmedState(trackID, on); break;
        case 3: tm->setMonitorState(trackID, on); break;
        case 4: tm->setStereoState(trackID, on); break;
        default: break;
    }
}

void wireArrangerTrackControl(ArrangerTrackControlAction* a) {
    a->doAction = [a]() {
        NodeManager& nm = requireManager(a->p, a->managerPath);
        auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(a->nodeID)));
        if (!node || !node->tracks)
            throw std::runtime_error("ArrangerTrackControlAction::doAction: arranger missing");
        setArrangerTrackControlValue(node->tracks, a->trackID, a->control, a->after);
    };
    a->undoAction = [a]() {
        NodeManager& nm = requireManager(a->p, a->managerPath);
        auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(a->nodeID)));
        if (!node || !node->tracks)
            throw std::runtime_error("ArrangerTrackControlAction::undoAction: arranger missing");
        setArrangerTrackControlValue(node->tracks, a->trackID, a->control, a->before);
    };
}

const char* controlName(int control) {
    switch (control) {
        case 0: return "Solo Track ";
        case 1: return "Mute Track ";
        case 2: return "Arm Track ";
        case 3: return "Monitor Track ";
        case 4: return "Stereoize Track ";
        default: return "Track ";
    }
}
} // namespace

ArrangerTrackControlAction::ArrangerTrackControlAction(Project* p, std::vector<int> managerPath, int nodeID,
                                                     uint16_t trackID, int control) :
        ProjectAction(p, ArrangerTrackControl),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        trackID(trackID),
        control(control) {
    NodeManager& nm = requireManager(this->p, this->managerPath);
    auto* node = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
    if (node && node->tracks)
        before = arrangerTrackControlValue(node->tracks, this->trackID, this->control);
    after = !before;
    name = std::string(controlName(this->control)) + std::to_string(this->trackID);
    wireArrangerTrackControl(this);
}

ArrangerTrackControlAction::ArrangerTrackControlAction(Project* p, std::vector<int> managerPath, int nodeID,
                                                     uint16_t trackID, int control, bool before, bool after) :
        ProjectAction(p, ArrangerTrackControl),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        trackID(trackID),
        control(control),
        before(before),
        after(after) {
    skipInitialDo = true;
    name = std::string(controlName(this->control)) + std::to_string(this->trackID);
    wireArrangerTrackControl(this);
}

// ---------------------------------------------------------------------------
// RecordTakeAction
// ---------------------------------------------------------------------------
namespace {
json recordPairsToJson(const std::vector<std::pair<int, int>>& v) {
    json a = json::array();
    for (const auto& pr : v) a.push_back(json::array({pr.first, pr.second}));
    return a;
}

std::vector<std::pair<int, int>> recordPairsFromJson(const json& j) {
    std::vector<std::pair<int, int>> out;
    if (!j.is_array()) return out;
    for (const auto& el : j)
        if (el.is_array() && el.size() >= 2)
            out.push_back({el[0].get<int>(), el[1].get<int>()});
    return out;
}

// Encode an arbitrary beat value as a single prime-slot exponent: beatsFromVector returns
// log2(2^(num/den)) = num/den, so no grid snapping is needed.
std::vector<std::pair<int, int>> recordPairsFromBeat(double t) {
    if (std::fabs(t) < 1e-9) return {};
    long long den = 1000000;
    while (den > 1 && std::llround(t * static_cast<double>(den)) > std::numeric_limits<int>::max() / 2)
        den /= 10;
    const long long num = std::llround(t * static_cast<double>(den));
    if (num == 0) return {};
    return {{static_cast<int>(num), static_cast<int>(den)}};
}

std::vector<std::pair<int, int>> recordPairsFromPitch(float pitch) {
    if (std::fabs(pitch - 69.0f) < 1e-6f) return {};
    return {{static_cast<int>(std::llround((static_cast<double>(pitch) - 69.0) * 1000000.0)), 12000000}};
}

void buildRecordPositionReconcile(ElementManager* em, uint16_t trackID, double takeStart, double takeEnd,
                                  const std::vector<std::pair<int, int>>& startPairs,
                                  const std::vector<std::pair<int, int>>& endPairs,
                                  json& before, json& after) {
    before = json::array();
    after = json::array();
    if (!em) return;
    const double eps = 1e-7;

    for (GridElement* el : em->elements) {
        json b = json::array();
        json a = json::array();
        bool affected = false;
        for (size_t i = 0; i < el->positions.size(); ++i) {
            GridElement::Position* pos = el->positions[i];
            if (!pos || pos->trackID != trackID) continue;
            const double ps = Note::beatsFromVector(pos->rhythmVector);
            const double pe = Note::beatsFromVector(pos->rhythmEndVector);
            if (pe <= takeStart + eps || ps >= takeEnd - eps) continue;

            affected = true;
            json pj = GridElement::positionToJson(*pos);
            b.push_back({{"index", static_cast<int>(i)}, {"pos", pj}});

            const bool startsBefore = ps < takeStart - eps;
            const bool endsAfter = pe > takeEnd + eps;
            if (startsBefore && endsAfter) {
                json left = pj;
                left["rhythmEndVector"] = recordPairsToJson(startPairs);
                json right = pj;
                right["rhythmVector"] = recordPairsToJson(endPairs);
                const auto off = recordPairsFromJson(pj["startOffsetPairs"]);
                const auto delta = recordPairsFromBeat(takeEnd - ps);
                right["startOffsetPairs"] = recordPairsToJson(addVec(off, delta));
                right["id"] = static_cast<int>(em->position_id_pool.newID());
                a.push_back({{"index", static_cast<int>(i)}, {"pos", left}});
                a.push_back({{"index", static_cast<int>(i) + 1}, {"pos", right}});
            } else if (startsBefore) {
                json left = pj;
                left["rhythmEndVector"] = recordPairsToJson(startPairs);
                a.push_back({{"index", static_cast<int>(i)}, {"pos", left}});
            } else if (endsAfter) {
                json right = pj;
                right["rhythmVector"] = recordPairsToJson(endPairs);
                const auto off = recordPairsFromJson(pj["startOffsetPairs"]);
                const auto delta = recordPairsFromBeat(takeEnd - ps);
                right["startOffsetPairs"] = recordPairsToJson(addVec(off, delta));
                a.push_back({{"index", static_cast<int>(i)}, {"pos", right}});
            }
            // Fully covered: listed in `before` only, so do removes it.
        }
        if (affected) {
            before.push_back({{"elementID", static_cast<int>(el->id)}, {"entries", b}});
            after.push_back({{"elementID", static_cast<int>(el->id)}, {"entries", a}});
        }
    }
}

void applyRecordPositionSet(ElementManager* em, const json& targetSet, const json& otherSet) {
    if (!em) return;
    std::map<int, std::vector<std::pair<int, json>>> target;
    std::map<int, std::vector<std::pair<int, json>>> other;
    auto parse = [](const json& set, std::map<int, std::vector<std::pair<int, json>>>& out) {
        if (!set.is_array()) return;
        for (const auto& e : set) {
            if (!e.contains("elementID") || !e.contains("entries")) continue;
            const int eid = e["elementID"].get<int>();
            for (const auto& entry : e["entries"])
                out[eid].push_back({entry.value("index", 0), entry.at("pos")});
        }
    };
    parse(targetSet, target);
    parse(otherSet, other);

    std::set<int> elemIDs;
    for (const auto& kv : target) elemIDs.insert(kv.first);
    for (const auto& kv : other) elemIDs.insert(kv.first);

    for (int eid : elemIDs) {
        GridElement* el = em->getElement(static_cast<uint16_t>(eid));
        if (!el) continue;

        std::set<int> targetIDs;
        for (const auto& [index, pos] : target[eid])
            targetIDs.insert(pos.value("id", -1));

        for (const auto& [index, pos] : other[eid]) {
            const int pid = pos.value("id", -1);
            if (!targetIDs.count(pid))
                el->removePositionById(pid);
        }

        auto entries = target[eid];
        std::stable_sort(entries.begin(), entries.end(),
            [](const auto& x, const auto& y) { return x.first < y.first; });
        for (const auto& [index, pos] : entries) {
            const int pid = pos.value("id", -1);
            GridElement::Position* existing = nullptr;
            for (auto* p : el->positions) {
                if (p && p->id == pid) { existing = p; break; }
            }
            if (existing)
                GridElement::applyPositionFromJson(existing, pos);
            else
                el->insertPositionAt(static_cast<size_t>(std::max(0, index)), pos);
        }
    }
}

void addTakeNotesToRegion(Region* region, const json& takeNotes) {
    if (!region) return;
    for (const auto& jn : takeNotes) {
        auto start = recordPairsFromJson(jn.value("rhythmVector", json::array()));
        auto end = recordPairsFromJson(jn.value("rhythmEndVector", json::array()));
        auto pitch = recordPairsFromJson(jn.value("pitchVector", json::array()));
        region->createNote(std::move(start), std::move(end), std::move(pitch));
        if (!region->notes.empty())
            region->notes.back()->channel = jn.value("channel", 0);
    }
}
} // namespace

RecordTakeAction::RecordTakeAction(Project* p, std::vector<int> managerPath, int nodeID, uint16_t trackID,
                                   const RecordedTake& take) :
        ProjectAction(p, RecordTake),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        trackID(trackID) {
    takeStartPairs = recordPairsFromBeat(take.startBeat);
    takeEndPairs = recordPairsFromBeat(take.endBeat);
    if (takeEndPairs.empty())
        takeEndPairs = recordPairsFromBeat(take.startBeat + 0.001);
    if (takeEndPairs.empty())
        takeEndPairs = {{1, 1000000}};
    isAudio = take.isAudio;
    filepath = take.filepath;
    if (!isAudio) {
        for (const auto& n : take.notes) {
            double s = n.startBeat - take.startBeat;
            if (s < 0.0) s = 0.0;
            double e = n.endBeat - take.startBeat;
            if (e <= s) e = s + 1e-4;
            json jn;
            jn["rhythmVector"] = recordPairsToJson(recordPairsFromBeat(s));
            jn["rhythmEndVector"] = recordPairsToJson(recordPairsFromBeat(e));
            jn["pitchVector"] = recordPairsToJson(recordPairsFromPitch(n.pitch));
            jn["channel"] = n.channel;
            takeNotes.push_back(std::move(jn));
        }
    }
    name = "Record Take";
    wireLambdas();
}

RecordTakeAction::RecordTakeAction(Project* p, std::vector<int> managerPath, int nodeID, uint16_t trackID,
                                   std::vector<std::pair<int, int>> takeStartPairs,
                                   std::vector<std::pair<int, int>> takeEndPairs,
                                   bool isAudio, std::string filepath, json takeNotes,
                                   int elementID, json elementSnapshot, bool snapshotValid,
                                   json beforePositions, json afterPositions) :
        ProjectAction(p, RecordTake),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        trackID(trackID),
        takeStartPairs(std::move(takeStartPairs)),
        takeEndPairs(std::move(takeEndPairs)),
        isAudio(isAudio),
        filepath(std::move(filepath)),
        takeNotes(std::move(takeNotes)),
        elementID(elementID),
        elementSnapshot(std::move(elementSnapshot)),
        snapshotValid(snapshotValid),
        beforePositions(std::move(beforePositions)),
        afterPositions(std::move(afterPositions)) {
    skipInitialDo = true;
    name = "Record Take";
    wireLambdas();
}

void RecordTakeAction::wireLambdas() {
    doAction = [this]() {
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        ArrangerNode* arr = undoResolveArrangerNode(this->p, this->managerPath, this->nodeID);
        if (!em || !arr)
            throw std::runtime_error("RecordTakeAction::doAction: arranger missing");

        const double takeStart = Note::beatsFromVector(this->takeStartPairs);
        const double takeEnd = Note::beatsFromVector(this->takeEndPairs);

        if (!this->snapshotValid) {
            buildRecordPositionReconcile(em, this->trackID, takeStart, takeEnd,
                                         this->takeStartPairs, this->takeEndPairs,
                                         this->beforePositions, this->afterPositions);

            if (this->isAudio) {
                AudioClip* clip = em->newAudioClip(this->filepath);
                if (!clip)
                    throw std::runtime_error("RecordTakeAction::doAction: failed to load recorded WAV");
                clip->createPos(this->takeStartPairs, this->takeEndPairs, this->trackID,
                                arr->rhythmEdoSubdivisionSteps, arr->rhythmEdoLowerVector,
                                arr->rhythmEdoUpperVector);
                this->elementID = static_cast<int>(clip->id);
                this->elementSnapshot = clip->toJSON();
            } else {
                Region* region = em->newRegion();
                addTakeNotesToRegion(region, this->takeNotes);
                region->createPos(this->takeStartPairs, this->takeEndPairs, this->trackID,
                                  arr->rhythmEdoSubdivisionSteps, arr->rhythmEdoLowerVector,
                                  arr->rhythmEdoUpperVector);
                this->elementID = static_cast<int>(region->id);
                this->elementSnapshot = region->toJSON();
            }
            this->snapshotValid = true;
        } else {
            if (this->isAudio)
                em->restoreAudioClipFromSnapshot(this->elementSnapshot);
            else
                em->restoreRegionFromSnapshot(this->elementSnapshot);
        }

        applyRecordPositionSet(em, this->afterPositions, this->beforePositions);
        arr->syncWidthsFromInputs();
        arr->nm->markTopologyDirty();
    };

    undoAction = [this]() {
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        ArrangerNode* arr = undoResolveArrangerNode(this->p, this->managerPath, this->nodeID);
        if (!em || !arr || !this->snapshotValid) return;

        em->removeElementById(static_cast<uint16_t>(this->elementID));
        applyRecordPositionSet(em, this->beforePositions, this->afterPositions);
        arr->syncWidthsFromInputs();
        arr->nm->markTopologyDirty();
    };
}

// ---------------------------------------------------------------------------
// TempoCurveEditAction
// ---------------------------------------------------------------------------
TempoCurveEditAction::TempoCurveEditAction(Project* p, json before, json after) :
        ProjectAction(p, TempoCurveEdit),
        before(std::move(before)),
        after(std::move(after)) {
    skipInitialDo = true;
    name = "Edit Tempo";
    doAction = [this]() {
        if (this->after.is_array()) {
            this->p->activeTempoCurve().points.clear();
            for (const auto& el : this->after) {
                CurvePoint pt;
                pt.v = el.value("v", 120.f);
                pt.shape.type = static_cast<CurveShape::Type>(el.value("shape", static_cast<int>(CurveShape::Single)));
                pt.shape.param = el.value("shapeParam", 0.f);
                if (el.contains("timeVec") && el["timeVec"].is_array())
                    for (const auto& pr : el["timeVec"])
                        if (pr.is_array() && pr.size() >= 2)
                            pt.timeVec.push_back({pr[0].get<int>(), pr[1].get<int>()});
                this->p->activeTempoCurve().points.push_back(std::move(pt));
            }
        }
    };
    undoAction = [this]() {
        if (this->before.is_array()) {
            this->p->activeTempoCurve().points.clear();
            for (const auto& el : this->before) {
                CurvePoint pt;
                pt.v = el.value("v", 120.f);
                pt.shape.type = static_cast<CurveShape::Type>(el.value("shape", static_cast<int>(CurveShape::Single)));
                pt.shape.param = el.value("shapeParam", 0.f);
                if (el.contains("timeVec") && el["timeVec"].is_array())
                    for (const auto& pr : el["timeVec"])
                        if (pr.is_array() && pr.size() >= 2)
                            pt.timeVec.push_back({pr[0].get<int>(), pr[1].get<int>()});
                this->p->activeTempoCurve().points.push_back(std::move(pt));
            }
        }
    };
}

