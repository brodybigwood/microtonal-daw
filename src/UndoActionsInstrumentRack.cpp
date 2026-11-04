#include "UndoManager.h"
#include "UndoInternal.h"
#include "NodeManager.h"
#include "nodes/instrumentrack/instrumentrack.h"
#include <stdexcept>

InstrumentRackInsertNodeAction::InstrumentRackInsertNodeAction(Project* p, std::vector<int> managerPath,
                                                               int rackNodeID, int trackID, json nodeSnapshot,
                                                               bool removeOnDo, std::string actionName)
    : ProjectAction(p, InstrumentRackInsertNode),
      managerPath(std::move(managerPath)),
      rackNodeID(rackNodeID),
      trackID(trackID),
      nodeSnapshot(std::move(nodeSnapshot)),
      removeOnDo(removeOnDo) {
    name = std::move(actionName);

    auto resolve = [this]() -> InstrumentRackNode* {
        try {
            NodeManager& nm = requireManager(this->p, this->managerPath);
            return dynamic_cast<InstrumentRackNode*>(nm.getNode(static_cast<uint16_t>(this->rackNodeID)));
        } catch (...) {
            return nullptr;
        }
    };
    auto doInsert = [this, resolve](bool remove) {
        InstrumentRackNode* r = resolve();
        if (!r) return;
        const int nid = this->nodeSnapshot.value("id", -1);
        if (remove) r->removeTrackNodeByID(this->trackID, nid);
        else r->insertTrackNodeFromSnapshot(this->trackID, this->nodeSnapshot);
    };
    doAction = [doInsert, this]() { doInsert(this->removeOnDo); };
    undoAction = [doInsert, this]() { doInsert(!this->removeOnDo); };
}

InstrumentRackEditUndoAction::InstrumentRackEditUndoAction(Project* p, std::vector<int> managerPath,
                                                           int nodeID, int op, int trackID,
                                                           uint16_t portID, uint16_t outPortID,
                                                           std::vector<int> order, json trackData,
                                                           std::string actionName)
    : ProjectAction(p, InstrumentRackEdit),
      managerPath(std::move(managerPath)),
      nodeID(nodeID),
      op(op),
      trackID(trackID),
      portID(portID),
      outPortID(outPortID),
      order(std::move(order)),
      trackData(std::move(trackData)) {
    name = std::move(actionName);

    auto resolve = [this]() -> InstrumentRackNode* {
        try {
            NodeManager& nm = requireManager(this->p, this->managerPath);
            return dynamic_cast<InstrumentRackNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        } catch (...) {
            return nullptr; // this graph does not contain the rack
        }
    };

    doAction = [this, resolve]() {
        InstrumentRackNode* r = resolve();
        if (!r) return;
        switch (this->op) {
            case Add:
                r->addTrackOnGraph(this->trackID, this->portID, this->outPortID);
                break;
            case Remove:
                r->removeTrackOnGraph(this->trackID);
                break;
            case Reorder: {
                std::vector<int> prev;
                for (auto& tp : r->tracks) prev.push_back(tp->id);
                this->trackData["prevOrder"] = prev;
                r->reorderTracksOnGraph(this->order);
                break;
            }
            case Solo:
                if (auto* t = r->trackByID(this->trackID)) t->solo = !t->solo;
                break;
            case Mute:
                if (auto* t = r->trackByID(this->trackID)) t->mute = !t->mute;
                break;
            case TrackNodes:
                r->applyTrackNodes(this->trackID, this->trackData["after"]);
                break;
        }
    };

    undoAction = [this, resolve]() {
        InstrumentRackNode* r = resolve();
        if (!r) return;
        switch (this->op) {
            case Add:
                r->removeTrackOnGraph(this->trackID);
                break;
            case Remove:
                r->restoreTrack(this->trackData);
                break;
            case Reorder:
                if (this->trackData.contains("prevOrder")) {
                    auto prevOrder = this->trackData["prevOrder"].get<std::vector<int>>();
                    r->reorderTracksOnGraph(prevOrder);
                }
                break;
            case Solo:
                if (auto* t = r->trackByID(this->trackID)) t->solo = !t->solo;
                break;
            case Mute:
                if (auto* t = r->trackByID(this->trackID)) t->mute = !t->mute;
                break;
            case TrackNodes:
                r->applyTrackNodes(this->trackID, this->trackData["before"]);
                break;
        }
    };
}

InstrumentRackNodeParamAction::InstrumentRackNodeParamAction(Project* p, std::vector<int> managerPath,
                                                             int rackNodeID, int trackID, int nodeID,
                                                             size_t paramIndex, float oldValue, float newValue,
                                                             std::string actionName)
    : ProjectAction(p, InstrumentRackNodeParam),
      managerPath(std::move(managerPath)),
      rackNodeID(rackNodeID),
      trackID(trackID),
      nodeID(nodeID),
      paramIndex(paramIndex),
      oldValue(oldValue),
      newValue(newValue) {
    skipInitialDo = true;
    name = std::move(actionName);

    auto apply = [this](float value) {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* r = dynamic_cast<InstrumentRackNode*>(nm.getNode(static_cast<uint16_t>(this->rackNodeID)));
        if (!r) return;
        Node* target = r->trackNodeByID(this->trackID, this->nodeID);
        if (!target || this->paramIndex >= target->params.size()) return;
        target->params[this->paramIndex]->value = value;
    };
    doAction = [this, apply]() { apply(this->newValue); };
    undoAction = [this, apply]() { apply(this->oldValue); };
}

bool InstrumentRackNodeParamAction::coalesceWith(ProjectAction* newer) {
    if (!newer || newer->type != InstrumentRackNodeParam) return false;
    auto* o = static_cast<InstrumentRackNodeParamAction*>(newer);
    if (o->rackNodeID != rackNodeID || o->trackID != trackID ||
        o->nodeID != nodeID || o->paramIndex != paramIndex)
        return false;
    newValue = o->newValue;
    return true;
}
