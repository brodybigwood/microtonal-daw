#include "UndoManager.h"
#include "nodes/parametriceq/parametriceq.h"
#include "nodes/vst/vstnode.h"
#include "nodes/arranger/arranger.h"
#include "SongRoll.h"
#include "PianoRollWindow.h"
#include "GridElement.h"
#include "AutomationCurve.h"
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

// Region, timeline-position and arranger-track actions (split from UndoManager.cpp).

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
    ArrangerNode* arr = undoResolveArrangerNode(p, this->managerPath, nodeID);
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
    };
    undoAction = [this]() {
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        if (!ge->removePositionById(this->positionID))
            throw std::runtime_error("CreatePositionAction::undoAction: position id missing");
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
        if (node->sl) node->sl->updateRhythmLines();
    };
}

void DeletePositionAction::wireLambdas() {
    doAction = [this]() {
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        if (!ge->removePositionById(this->positionID))
            throw std::runtime_error("DeletePositionAction::doAction: position id missing");
    };
    undoAction = [this]() {
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        ge->insertPositionAt(this->insertIndex, this->positionSnapshot);
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
        GridElement::applyPositionFromJson(pos, this->after);
    };
    undoAction = [this]() {
        GridElement::Position* pos = undoResolveElementPosition(this->p, this->managerPath, this->nodeID, this->elementID, this->positionID);
        GridElement::applyPositionFromJson(pos, this->before);
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
            tm->addTrackNow(static_cast<TrackType>(this->trackType), this->trackID, this->connectionID);
        if (!track)
            throw std::runtime_error("AddArrangerTrackAction::doAction: addTrackNow failed");
        this->trackID = track->id;
        if (track->connection)
            this->connectionID = track->connection->id;
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
        node->tracks->addTrackNow(static_cast<TrackType>(this->trackType), this->trackID, this->connectionID, this->trackIndex);
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
                node->elements->id_pool.fromJSON(positionIdPoolSnapshot);
        }
    };
}

