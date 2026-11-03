#include "UndoManager.h"
#include "UndoInternal.h"
#include "NodeManager.h"
#include "nodes/mixer/mixer.h"
#include <stdexcept>

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
