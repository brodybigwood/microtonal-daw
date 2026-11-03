#include "UndoManager.h"
#include "UndoInternal.h"
#include "NodeManager.h"
#include "nodes/mixer/mixer.h"
#include <stdexcept>

MixerChainNodeParamAction::MixerChainNodeParamAction(Project* p, std::vector<int> managerPath,
                                                     int mixerNodeID, int chainID, int chainNodeID,
                                                     size_t paramIndex, float oldValue, float newValue,
                                                     std::string actionName)
    : ProjectAction(p, MixerChainNodeParam),
      managerPath(std::move(managerPath)),
      mixerNodeID(mixerNodeID),
      chainID(chainID),
      chainNodeID(chainNodeID),
      paramIndex(paramIndex),
      oldValue(oldValue),
      newValue(newValue) {
    skipInitialDo = true;
    name = std::move(actionName);

    auto apply = [this](float value) {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* m = dynamic_cast<MixerNode*>(nm.getNode(static_cast<uint16_t>(this->mixerNodeID)));
        if (!m) return;
        Node* target = m->chainNodeByID(this->chainID, this->chainNodeID);
        if (!target || this->paramIndex >= target->params.size()) return;
        target->params[this->paramIndex]->value = value;
    };
    doAction = [this, apply]() { apply(this->newValue); };
    undoAction = [this, apply]() { apply(this->oldValue); };
}

bool MixerChainNodeParamAction::coalesceWith(ProjectAction* newer) {
    if (!newer || newer->type != MixerChainNodeParam) return false;
    auto* o = static_cast<MixerChainNodeParamAction*>(newer);
    if (o->mixerNodeID != mixerNodeID || o->chainID != chainID ||
        o->chainNodeID != chainNodeID || o->paramIndex != paramIndex)
        return false;
    newValue = o->newValue;
    return true;
}

MixerChainEditUndoAction::MixerChainEditUndoAction(Project* p, std::vector<int> managerPath,
                                                   int nodeID, int op, int chainID,
                                                   uint16_t portID, std::vector<int> order,
                                                   json chainData, std::string actionName)
    : ProjectAction(p, MixerChainEdit),
      managerPath(std::move(managerPath)),
      nodeID(nodeID),
      op(op),
      chainID(chainID),
      portID(portID),
      order(std::move(order)),
      chainData(std::move(chainData)) {
    name = std::move(actionName);

    auto resolve = [this]() -> MixerNode* {
        try {
            NodeManager& nm = requireManager(this->p, this->managerPath);
            return dynamic_cast<MixerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        } catch (...) {
            return nullptr; // this graph does not contain the mixer
        }
    };

    doAction = [this, resolve]() {
        MixerNode* m = resolve();
        if (!m) return;
        switch (this->op) {
            case Add:
                m->addChainOnGraph(this->chainID, this->portID);
                break;
            case Remove:
                m->removeChainOnGraph(this->chainID);
                break;
            case Reorder: {
                std::vector<int> prev;
                for (auto& cp : m->chains) prev.push_back(cp->id);
                this->chainData["prevOrder"] = prev;
                m->reorderChainsOnGraph(this->order);
                break;
            }
            case Solo:
                if (auto* c = m->chainByID(this->chainID)) c->solo = !c->solo;
                break;
            case Mute:
                if (auto* c = m->chainByID(this->chainID)) c->mute = !c->mute;
                break;
            case ChainNodes:
                m->applyChainNodes(this->chainID, this->chainData["after"]);
                break;
        }
    };

    undoAction = [this, resolve]() {
        MixerNode* m = resolve();
        if (!m) return;
        switch (this->op) {
            case Add:
                m->removeChainOnGraph(this->chainID);
                break;
            case Remove:
                m->restoreChain(this->chainData);
                break;
            case Reorder:
                if (this->chainData.contains("prevOrder")) {
                    auto prevOrder = this->chainData["prevOrder"].get<std::vector<int>>();
                    m->reorderChainsOnGraph(prevOrder);
                }
                break;
            case Solo:
                if (auto* c = m->chainByID(this->chainID)) c->solo = !c->solo;
                break;
            case Mute:
                if (auto* c = m->chainByID(this->chainID)) c->mute = !c->mute;
                break;
            case ChainNodes:
                m->applyChainNodes(this->chainID, this->chainData["before"]);
                break;
        }
    };
}
