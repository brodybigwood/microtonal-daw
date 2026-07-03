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

// Parameter and modulation actions (split from UndoManager.cpp).

AddModSourceUndoAction::AddModSourceUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath) :
        ProjectAction(p, AddModSourceUndo),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        paramPath(std::move(paramPath)) {
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                            : nm.getNode(static_cast<uint16_t>(this->nodeID));
        if (!target)
            throw std::runtime_error("AddModSourceUndoAction::doAction: target node not found");
        target->addModSourceNow(this->paramPath);
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                            : nm.getNode(static_cast<uint16_t>(this->nodeID));
        if (!target)
            throw std::runtime_error("AddModSourceUndoAction::undoAction: target node not found");
        Parameter* param = target->resolveParameterPath(this->paramPath);
        if (!param || param->modulators.empty())
            throw std::runtime_error("AddModSourceUndoAction::undoAction: no modulator to remove");
        target->removeModSourceNow(this->paramPath, param->modulators.size() - 1);
    };
}

RemoveModSourceUndoAction::RemoveModSourceUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath,
                                                     size_t modIndex) :
        ProjectAction(p, RemoveModSourceUndo),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        paramPath(std::move(paramPath)),
        modIndex(modIndex) {
    // Snapshot the modulator tree and all connection wiring before doAction runs.
    NodeManager& nm = requireManager(this->p, this->managerPath);
    Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                 : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                        : nm.getNode(static_cast<uint16_t>(this->nodeID));
    if (!target) {
        name = "Remove Mod Source (error: node not found)";
        doAction = []() {};
        undoAction = []() {};
        return;
    }
    Parameter* param = target->resolveParameterPath(this->paramPath);
    if (!param || this->modIndex >= param->modulators.size()) {
        name = "Remove Mod Source (error: bad path)";
        doAction = []() {};
        undoAction = []() {};
        return;
    }
    savedModulator = param->modulators[this->modIndex];

    // Collect every connection in the modulator tree with its wiring state.
    std::function<void(Modulator*)> collect = [&](Modulator* mod) {
        for (auto* nested : mod->depth.modulators) collect(nested);
        if (mod->sourceConnection) {
            SavedConn sc;
            sc.conn = mod->sourceConnection;
            auto& conns = target->inputs.connections;
            auto it = std::find(conns.begin(), conns.end(), sc.conn);
            sc.index = (it != conns.end()) ? static_cast<size_t>(it - conns.begin()) : 0;
            sc.wasConnected = sc.conn->is_connected;
            if (sc.wasConnected) {
                sc.srcNode = static_cast<uint16_t>(sc.conn->output_node);
                sc.srcCon  = static_cast<uint16_t>(sc.conn->output_connection);
            }
            savedConns.push_back(sc);
        }
    };
    collect(savedModulator);

    name = "Remove Mod Source";

    doAction = [this, target, param]() {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        // 1. Sever every connected cable.
        for (auto& sc : this->savedConns) {
            if (sc.wasConnected) {
                nm2.severConnectionNow(sc.srcNode, sc.srcCon,
                    static_cast<uint16_t>(target->id), sc.conn->id);
                sc.conn->is_connected = false;
            }
        }
        // 2. Remove connections from inputs (save, don't delete).
        for (auto& sc : this->savedConns) {
            auto& conns = target->inputs.connections;
            auto it = std::find(conns.begin(), conns.end(), sc.conn);
            if (it != conns.end()) {
                target->inputs.ids.erase(sc.conn->id);
                conns.erase(it);
            }
        }
        target->inputs.ids.clear();
        for (size_t i = 0; i < target->inputs.connections.size(); ++i)
            target->inputs.ids[target->inputs.connections[i]->id] = static_cast<uint16_t>(i);
        target->makeConnectionRects();
        nm2.markTopologyDirty();
        // 3. Remove modulator from parameter (save, don't delete).
        if (this->modIndex < param->modulators.size() && param->modulators[this->modIndex] == this->savedModulator)
            param->modulators.erase(param->modulators.begin() + static_cast<ptrdiff_t>(this->modIndex));
    };

    undoAction = [this, target, param]() {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        // 1. Re-insert modulator into parameter at original index.
        size_t insIdx = std::min(this->modIndex, param->modulators.size());
        param->modulators.insert(param->modulators.begin() + static_cast<ptrdiff_t>(insIdx), this->savedModulator);
        // 2. Re-insert connections into inputs at original positions.
        for (auto& sc : this->savedConns) {
            auto& conns = target->inputs.connections;
            size_t ci = std::min(sc.index, conns.size());
            conns.insert(conns.begin() + static_cast<ptrdiff_t>(ci), sc.conn);
        }
        target->inputs.ids.clear();
        for (size_t i = 0; i < target->inputs.connections.size(); ++i)
            target->inputs.ids[target->inputs.connections[i]->id] = static_cast<uint16_t>(i);
        target->makeConnectionRects();
        nm2.markTopologyDirty();
        // 3. Reconnect cables.
        for (auto& sc : this->savedConns) {
            if (sc.wasConnected) {
                nm2.makeNodeConnectionNow(sc.srcNode, sc.srcCon,
                    static_cast<uint16_t>(target->id), sc.conn->id);
            }
        }
    };
}

SetParamValueUndoAction::SetParamValueUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath,
                                                 float oldValue, float newValue, std::string actionName) :
        ProjectAction(p, SetParamValue),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        paramPath(std::move(paramPath)),
        oldValue(oldValue),
        newValue(newValue) {
    skipInitialDo = true;
    name = std::move(actionName);
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                           : nm.getNode(static_cast<uint16_t>(this->nodeID));
        if (!target) throw std::runtime_error("SetParamValueUndoAction::doAction: node not found");
        Parameter* param = target->resolveParameterPath(this->paramPath);
        if (!param) throw std::runtime_error("SetParamValueUndoAction::doAction: parameter not found");
        param->value = this->newValue;
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                           : nm.getNode(static_cast<uint16_t>(this->nodeID));
        if (!target) throw std::runtime_error("SetParamValueUndoAction::undoAction: node not found");
        Parameter* param = target->resolveParameterPath(this->paramPath);
        if (!param) throw std::runtime_error("SetParamValueUndoAction::undoAction: parameter not found");
        param->value = this->oldValue;
    };
}

ToggleModulatorCenteredUndoAction::ToggleModulatorCenteredUndoAction(Project* p, std::vector<int> managerPath, int nodeID,
                                                                     std::vector<size_t> paramPath, size_t modIndex,
                                                                     bool oldCentered, bool newCentered, float oldDepth, float newDepth) :
        ProjectAction(p, ToggleModulatorCentered),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        paramPath(std::move(paramPath)),
        modIndex(modIndex),
        oldCentered(oldCentered),
        newCentered(newCentered),
        oldDepth(oldDepth),
        newDepth(newDepth) {
    skipInitialDo = true;
    name = "Toggle Centered";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                           : nm.getNode(static_cast<uint16_t>(this->nodeID));
        if (!target) throw std::runtime_error("ToggleModulatorCenteredUndoAction::doAction: node not found");
        Parameter* param = target->resolveParameterPath(this->paramPath);
        if (!param || this->modIndex >= param->modulators.size())
            throw std::runtime_error("ToggleModulatorCenteredUndoAction::doAction: modulator not found");
        auto* mod = param->modulators[this->modIndex];
        mod->centered = this->newCentered;
        mod->depth.value = this->newDepth;
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                           : nm.getNode(static_cast<uint16_t>(this->nodeID));
        if (!target) throw std::runtime_error("ToggleModulatorCenteredUndoAction::undoAction: node not found");
        Parameter* param = target->resolveParameterPath(this->paramPath);
        if (!param || this->modIndex >= param->modulators.size())
            throw std::runtime_error("ToggleModulatorCenteredUndoAction::undoAction: modulator not found");
        auto* mod = param->modulators[this->modIndex];
        mod->centered = this->oldCentered;
        mod->depth.value = this->oldDepth;
    };
}

