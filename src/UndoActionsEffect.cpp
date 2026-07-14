#include "UndoManager.h"
#include "JsonBytes.h"
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

// EQ-band and VST plugin/parameter actions (split from UndoManager.cpp).

AddEQBandAction::AddEQBandAction(Project* p, std::vector<int> managerPath, int nodeID, int bandIndex, json bandState)
    : ProjectAction(p, AddEQBand)
    , managerPath(std::move(managerPath))
    , nodeID(nodeID)
    , bandIndex(bandIndex)
    , bandState(std::move(bandState))
{
    skipInitialDo = true;
    name = "Add EQ Band";

    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* eq = dynamic_cast<ParametricEQNode*>(node);
        if (!eq) throw std::runtime_error("AddEQBandAction::doAction: node not found or not an EQ");
        eq->addBand(this->bandIndex);
        if (this->bandIndex >= 0 && this->bandIndex < static_cast<int>(eq->bands.size())) {
            auto& b = *eq->bands[this->bandIndex];
            b.type.value = this->bandState.value("type", 0.0f);
            b.frequency.value = this->bandState.value("freq", 0.5f);
            b.gain.value = this->bandState.value("gain", 0.5f);
            b.q.value = this->bandState.value("q", 0.1f);
            b.coeffDirty = true;
        }
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* eq = dynamic_cast<ParametricEQNode*>(node);
        if (!eq) throw std::runtime_error("AddEQBandAction::undoAction: node not found or not an EQ");
        eq->removeBand(this->bandIndex);
    };
}

RemoveEQBandAction::RemoveEQBandAction(Project* p, std::vector<int> managerPath, int nodeID, int bandIndex, json bandState)
    : ProjectAction(p, RemoveEQBand)
    , managerPath(std::move(managerPath))
    , nodeID(nodeID)
    , bandIndex(bandIndex)
    , bandState(std::move(bandState))
{
    skipInitialDo = true;
    name = "Remove EQ Band";

    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* eq = dynamic_cast<ParametricEQNode*>(node);
        if (!eq) throw std::runtime_error("RemoveEQBandAction::doAction: node not found or not an EQ");
        eq->removeBand(this->bandIndex);
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* eq = dynamic_cast<ParametricEQNode*>(node);
        if (!eq) throw std::runtime_error("RemoveEQBandAction::undoAction: node not found or not an EQ");
        eq->addBand(this->bandIndex);
        if (this->bandIndex >= 0 && this->bandIndex < static_cast<int>(eq->bands.size())) {
            auto& b = *eq->bands[this->bandIndex];
            b.type.value = this->bandState.value("type", 0.0f);
            b.frequency.value = this->bandState.value("freq", 0.5f);
            b.gain.value = this->bandState.value("gain", 0.5f);
            b.q.value = this->bandState.value("q", 0.1f);
            b.coeffDirty = true;
        }
    };
}

// ============================================================================
// VstParameterChangeAction
// ============================================================================

VstParameterChangeAction::VstParameterChangeAction(Project* p, std::vector<int> managerPath, int nodeID,
                                                     uint32_t paramID, float oldValue, float newValue)
    : ProjectAction(p, VstParameterChange)
    , managerPath(std::move(managerPath))
    , nodeID(nodeID)
    , paramID(paramID)
    , oldValue(oldValue)
    , newValue(newValue)
{
    skipInitialDo = true;
    name = "VST Parameter Change";

    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* vst = dynamic_cast<VstNode*>(node);
        if (!vst) throw std::runtime_error("VstParameterChangeAction::doAction: node not found or not VstNode");
        if (!vst->plugin) throw std::runtime_error("VstParameterChangeAction::doAction: plugin is null");
        vst->restoringState = true;
        vst->plugin->setParameterValue(static_cast<int>(this->paramID), this->newValue);
        vst->restoringState = false;
        vst->vstStateBaselineDirty = true;
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* vst = dynamic_cast<VstNode*>(node);
        if (!vst) throw std::runtime_error("VstParameterChangeAction::undoAction: node not found or not VstNode");
        if (!vst->plugin) throw std::runtime_error("VstParameterChangeAction::undoAction: plugin is null");
        vst->restoringState = true;
        vst->plugin->setParameterValue(static_cast<int>(this->paramID), this->oldValue);
        vst->restoringState = false;
        vst->vstStateBaselineDirty = true;
    };
}

// ============================================================================
// VstLoadPluginAction
// ============================================================================

VstLoadPluginAction::VstLoadPluginAction(Project* p, std::vector<int> managerPath, int nodeID,
                                           json oldState, json newState)
    : ProjectAction(p, VstLoadPlugin)
    , managerPath(std::move(managerPath))
    , nodeID(nodeID)
    , oldState(std::move(oldState))
    , newState(std::move(newState))
{
    skipInitialDo = true;
    name = "Load VST Plugin";

    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* vst = dynamic_cast<VstNode*>(node);
        if (!vst) return;
        std::string path = this->newState.is_null() ? "" : this->newState.value("path", "");
        vst->loadPlugin(path);
        if (!this->newState.is_null()) {
            vst->bypass.value = this->newState.value("bypass", 0.0f);
            if (this->newState.contains("compState") && vst->plugin)
                vst->plugin->setComponentState(jsonBytesDecode(this->newState["compState"]));
            if (this->newState.contains("ctrlState") && vst->plugin)
                vst->plugin->setControllerState(jsonBytesDecode(this->newState["ctrlState"]));
        }
        vst->vstStateBaselineDirty = true;
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* vst = dynamic_cast<VstNode*>(node);
        if (!vst) return;
        if (vst->plugin) vst->plugin->hideEditor();
        std::string path = this->oldState.is_null() ? "" : this->oldState.value("path", "");
        vst->loadPlugin(path);
        if (!this->oldState.is_null()) {
            vst->bypass.value = this->oldState.value("bypass", 0.0f);
            if (this->oldState.contains("compState") && vst->plugin)
                vst->plugin->setComponentState(jsonBytesDecode(this->oldState["compState"]));
            if (this->oldState.contains("ctrlState") && vst->plugin)
                vst->plugin->setControllerState(jsonBytesDecode(this->oldState["ctrlState"]));
        }
        vst->vstStateBaselineDirty = true;
    };
}

// ============================================================================
// VstStateChangeAction
// ============================================================================

static void vstApplyStateJson(Project* p, const std::vector<int>& managerPath, int nodeID,
                              const json& state, const char* ctx) {
    NodeManager& nm = requireManager(p, managerPath);
    Node* node = nm.getNode(static_cast<uint16_t>(nodeID));
    auto* vst = dynamic_cast<VstNode*>(node);
    if (!vst) throw std::runtime_error(std::string(ctx) + ": node not found or not VstNode");
    if (!vst->plugin) throw std::runtime_error(std::string(ctx) + ": plugin is null");
    vst->restoringState = true;
    if (state.contains("compState"))
        vst->plugin->setComponentState(jsonBytesDecode(state["compState"]));
    if (state.contains("ctrlState"))
        vst->plugin->setControllerState(jsonBytesDecode(state["ctrlState"]));
    vst->restoringState = false;
    vst->vstStateBaselineDirty = true;
}

VstStateChangeAction::VstStateChangeAction(Project* p, std::vector<int> managerPath, int nodeID,
                                           json oldState, json newState, bool liveCreated)
    : ProjectAction(p, VstStateChange)
    , managerPath(std::move(managerPath))
    , nodeID(nodeID)
    , oldState(std::move(oldState))
    , newState(std::move(newState))
    , skipFirstReplay(liveCreated)
{
    skipInitialDo = true;
    name = "VST Plugin Edit";

    doAction = [this]() {
        if (this->skipFirstReplay) {
            this->skipFirstReplay = false;
            return;
        }
        vstApplyStateJson(this->p, this->managerPath, this->nodeID, this->newState,
                          "VstStateChangeAction::doAction");
    };

    undoAction = [this]() {
        vstApplyStateJson(this->p, this->managerPath, this->nodeID, this->oldState,
                          "VstStateChangeAction::undoAction");
    };
}

