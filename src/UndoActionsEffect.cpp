#include "UndoManager.h"
#include "JsonBytes.h"
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

// VST plugin/parameter actions (split from UndoManager.cpp).

// ============================================================================
// VstParameterChangeAction
// ============================================================================

VstParameterChangeAction::VstParameterChangeAction(Project* p, std::vector<int> managerPath, int nodeID,
                                                     uint32_t paramID, float oldValue, float newValue,
                                                     uint64_t gestureID)
    : ProjectAction(p, VstParameterChange)
    , managerPath(std::move(managerPath))
    , nodeID(nodeID)
    , paramID(paramID)
    , oldValue(oldValue)
    , newValue(newValue)
    , gestureID(gestureID)
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
        vst->markRackDirty();
        vst->vstStateBaselineDirty = true;
        vst->requestStatePollSoon();
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
        vst->markRackDirty();
        vst->vstStateBaselineDirty = true;
        vst->requestStatePollSoon();
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
        size_t cls = this->newState.is_null() ? 0 : this->newState.value("classIndex", static_cast<size_t>(0));
        vst->loadPlugin(path, false, cls);
        if (!this->newState.is_null()) {
            vst->bypass.value = this->newState.value("bypass", 0.0f);
            if (vst->plugin) {
                if (vst->plugin->isEditorOpen()) vst->plugin->hideEditor();
                vst->plugin->beginStateRestore();
                if (this->newState.contains("compState"))
                    vst->plugin->setComponentState(jsonBytesDecode(this->newState["compState"]));
                if (this->newState.contains("ctrlState"))
                    vst->plugin->setControllerState(jsonBytesDecode(this->newState["ctrlState"]));
                vst->plugin->endStateRestore();
            }
        }
        vst->markRackDirty();
        vst->vstStateBaselineDirty = true;
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* vst = dynamic_cast<VstNode*>(node);
        if (!vst) return;
        if (vst->plugin) vst->plugin->hideEditor();
        std::string path = this->oldState.is_null() ? "" : this->oldState.value("path", "");
        size_t cls = this->oldState.is_null() ? 0 : this->oldState.value("classIndex", static_cast<size_t>(0));
        vst->loadPlugin(path, false, cls);
        if (!this->oldState.is_null()) {
            vst->bypass.value = this->oldState.value("bypass", 0.0f);
            if (vst->plugin) {
                if (vst->plugin->isEditorOpen()) vst->plugin->hideEditor();
                vst->plugin->beginStateRestore();
                if (this->oldState.contains("compState"))
                    vst->plugin->setComponentState(jsonBytesDecode(this->oldState["compState"]));
                if (this->oldState.contains("ctrlState"))
                    vst->plugin->setControllerState(jsonBytesDecode(this->oldState["ctrlState"]));
                vst->plugin->endStateRestore();
            }
        }
        vst->markRackDirty();
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
    // Apply state while deactivated (Ardour's VST3PI::load_state sequence).
    if (vst->plugin->isEditorOpen()) vst->plugin->hideEditor();
    vst->restoringState = true;
    vst->plugin->beginStateRestore();
    if (state.contains("compState"))
        vst->plugin->setComponentState(jsonBytesDecode(state["compState"]));
    if (state.contains("ctrlState"))
        vst->plugin->setControllerState(jsonBytesDecode(state["ctrlState"]));
    vst->plugin->endStateRestore();
    vst->restoringState = false;
    vst->markRackDirty();
    vst->vstStateBaselineDirty = true;
    vst->requestStatePollSoon();
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

