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

// Action name registry / schema / scripted invocation (split from UndoManager.cpp).

const std::unordered_map<std::string, ActionType>& UndoManager::actionRegistry() {
    static const std::unordered_map<std::string, ActionType> reg = {
        {"create_note", CreateNote},
        {"add_arranger_track", AddArrangerTrack},
        {"add_node", AddNode},
        {"remove_node", RemoveNode},
        {"make_node_connection", MakeNodeConnection},
        {"sever_node_connection", SeverNodeConnection},
        {"create_region", CreateRegion},
        {"delete_region", DeleteRegion},
        {"create_position", CreatePosition},
        {"delete_position", DeletePosition},
        {"move_element_position", MoveElementPosition},
        {"move_embedded_window", MoveEmbeddedWindow},
        {"resize_embedded_window", ResizeEmbeddedWindow},
        {"add_eq_band", AddEQBand},
        {"remove_eq_band", RemoveEQBand},
        {"vst_param_change", VstParameterChange},
        {"vst_load_plugin", VstLoadPlugin},
        {"vst_state_change", VstStateChange},
        {"toggle_piano_roll_window", TogglePianoRollWindow},
        {"create_automation_curve", CreateAutomationCurve},
        {"modify_curve_points", ModifyCurvePoints},
        {"create_audio_clip", CreateAudioClip},
        {"map_parameter", MapParameter},
        {"unmap_parameter", UnmapParameter},
        {"paramnode_add_mod_row", ParamNodeAddModRow},
        {"paramnode_remove_mod_row", ParamNodeRemoveModRow},
        {"paramnode_toggle_centered", ParamNodeToggleCentered},
        {"tempo_curve_edit", TempoCurveEdit},
    };
    return reg;
}

std::string UndoManager::actionSchema(const std::string& actionName) {
    if (actionName == "add_node") {
        return R"({"managerPath":[int,...],"nodeType":int,"x":float,"y":float})";
    }
    if (actionName == "remove_node") {
        return R"({"managerPath":[int,...],"nodeID":int})";
    }
    if (actionName == "make_node_connection" || actionName == "sever_node_connection") {
        return R"({"managerPath":[int,...],"srcNodeID":int,"srcConID":int,"dstNodeID":int,"dstConID":int})";
    }
    if (actionName == "move_node") {
        return R"({"managerPath":[int,...],"nodeID":int,"toX":float,"toY":float})";
    }
    if (actionName == "move_embedded_window") {
        return R"({"managerPath":[int,...],"ewID":int,"toX":float,"toY":float})";
    }
    if (actionName == "resize_embedded_window") {
        return R"({"managerPath":[int,...],"ewID":int,"toX":float,"toY":float,"toW":float,"toH":float})";
    }
    if (actionName == "add_arranger_track") {
        return R"({"managerPath":[int,...],"nodeID":int,"trackType":int})";
    }
    if (actionName == "create_note") {
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"start":timeVec_json,"length":timeVec_json,"pitch":float,"pitchVector":[[int,int],...]})";
    }
    if (actionName == "create_region") {
        return R"({"managerPath":[int,...],"nodeID":int})";
    }
    if (actionName == "delete_region") {
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int})";
    }
    if (actionName == "create_position") {
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"start":timeVec_json,"trackID":int})";
    }
    if (actionName == "delete_position") {
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"positionID":int})";
    }
    if (actionName == "move_element_position") {
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"positionID":int,"before":object,"after":object})";
    }
    if (actionName == "add_eq_band") {
        return R"({"managerPath":[int,...],"nodeID":int,"bandIndex":int,"bandState":object})";
    }
    if (actionName == "remove_eq_band") {
        return R"({"managerPath":[int,...],"nodeID":int,"bandIndex":int,"bandState":object})";
    }
    if (actionName == "vst_param_change") {
        return R"({"managerPath":[int,...],"nodeID":int,"paramID":int,"oldValue":float,"newValue":float})";
    }
    if (actionName == "vst_load_plugin") {
        return R"({"managerPath":[int,...],"nodeID":int,"oldState":object,"newState":object})";
    }
    if (actionName == "vst_state_change") {
        return R"({"managerPath":[int,...],"nodeID":int,"oldState":object,"newState":object})";
    }
    if (actionName == "toggle_piano_roll_window") {
        return R"({"managerPath":[int,...],"arrangerNodeID":int,"regionID":int,"ewID":int,"x":float,"y":float,"w":float,"h":float,"open":bool})";
    }
    throw std::runtime_error("actionSchema: unknown action name");
}

bool UndoManager::runRegisteredAction(const std::string& actionName, const json& params, std::string& error) {
    (void)error;
    if (!head || !head->p)
        throw std::runtime_error("runRegisteredAction: undo manager not initialized");
    if (!params.is_object())
        throw std::runtime_error("runRegisteredAction: params must be a json object");
    auto it = actionRegistry().find(actionName);
    if (it == actionRegistry().end())
        throw std::runtime_error("runRegisteredAction: unknown action");
    ProjectAction* pa = nullptr;
    switch (it->second) {
        case AddNode:
            pa = new AddNodeAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeType").get<int>(),
                params.at("x").get<float>(), params.at("y").get<float>());
            break;
        case RemoveNode:
            pa = new RemoveNodeAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>());
            break;
        case MakeNodeConnection:
            pa = new MakeNodeConnectionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("srcNodeID").get<int>(),
                params.at("srcConID").get<int>(), params.at("dstNodeID").get<int>(), params.at("dstConID").get<int>());
            break;
        case SeverNodeConnection:
            pa = new SeverNodeConnectionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("srcNodeID").get<int>(),
                params.at("srcConID").get<int>(), params.at("dstNodeID").get<int>(), params.at("dstConID").get<int>());
            break;
        case ReassignNodeConnection: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            int oldCount = params.at("oldConnCount").get<int>();
            ConnIDs oldCxns[2];
            if (params.contains("oldConns")) {
                int i = 0;
                for (auto& oc : params.at("oldConns")) {
                    if (i >= 2) break;
                    oldCxns[i].srcNodeID = oc.at("srcNodeID").get<int>();
                    oldCxns[i].srcConID = oc.at("srcConID").get<int>();
                    oldCxns[i].dstNodeID = oc.at("dstNodeID").get<int>();
                    oldCxns[i].dstConID = oc.at("dstConID").get<int>();
                    oldCxns[i].existed = oc.at("existed").get<bool>();
                    ++i;
                }
            }
            pa = new ReassignNodeConnectionAction(head->p, std::move(mp), oldCxns, oldCount,
                params.at("newSrcNodeID").get<int>(), params.at("newSrcConID").get<int>(),
                params.at("newDstNodeID").get<int>(), params.at("newDstConID").get<int>());
            break;
        }
        case MoveEmbeddedWindow: {
            auto managerPath = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, managerPath);
            EmbeddedWindow* ew = nullptr;
            if (params.contains("ewID") && nm.ne)
                ew = nm.ne->getEmbeddedWindowById(params.at("ewID").get<int>());
            if (!ew)
                throw std::runtime_error("runRegisteredAction move_embedded_window: window not found");
            pa = new MoveEmbeddedWindowAction(head->p, managerPath, params.at("ewID").get<int>(),
                ew->x, ew->y, params.at("toX").get<float>(), params.at("toY").get<float>());
            break;
        }
        case ResizeEmbeddedWindow: {
            auto managerPath = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, managerPath);
            EmbeddedWindow* ew = nullptr;
            if (params.contains("ewID") && nm.ne)
                ew = nm.ne->getEmbeddedWindowById(params.at("ewID").get<int>());
            if (!ew)
                throw std::runtime_error("runRegisteredAction resize_embedded_window: window not found");
            pa = new ResizeEmbeddedWindowAction(head->p, managerPath, params.at("ewID").get<int>(),
                ew->x, ew->y, ew->w, ew->h,
                params.at("toX").get<float>(), params.at("toY").get<float>(),
                params.at("toW").get<float>(), params.at("toH").get<float>());
            break;
        }
        case AddArrangerTrack:
            pa = new AddArrangerTrackAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(), params.at("trackType").get<int>());
            break;
        case RemoveArrangerTrack:
            pa = new RemoveArrangerTrackAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(), params.at("trackType").get<int>(), params.at("trackID").get<int>(), params.at("connectionID").get<int>());
            break;
        case CreateNote: {
            auto managerPath = params.at("managerPath").get<std::vector<int>>();
            requireManager(head->p, managerPath);
            std::vector<std::pair<int, int>> pairs;
            for (const auto& el : params.at("pitchVector")) {
                pairs.push_back({el.at(0).get<int>(), el.at(1).get<int>()});
            }
            std::vector<std::pair<int,int>> rPairs;
            if (params.contains("rhythmVector") && params["rhythmVector"].is_array()) {
                for (const auto& el : params["rhythmVector"])
                    if (el.is_array() && el.size() >= 2)
                        rPairs.push_back({el[0].get<int>(), el[1].get<int>()});
            }
            std::vector<std::pair<int,int>> rEndPairs;
            if (params.contains("rhythmEndVector") && params["rhythmEndVector"].is_array()) {
                for (const auto& el : params["rhythmEndVector"])
                    if (el.is_array() && el.size() >= 2)
                        rEndPairs.push_back({el[0].get<int>(), el[1].get<int>()});
            }
            if (rEndPairs.empty()) rEndPairs = rPairs;
            pa = new CreateNoteAction(head->p, managerPath, params.at("nodeID").get<int>(), params.at("regionID").get<int>(),
                std::move(rPairs), std::move(rEndPairs),
                std::move(pairs));
            break;
        }
        case CreateRegion:
            pa = new CreateRegionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>());
            break;
        case DeleteRegion:
            pa = new DeleteRegionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("regionID").get<int>());
            break;
        case CreatePosition: {
            auto readPairs = [](const json& j) {
                std::vector<std::pair<int,int>> out;
                for (const auto& el : j)
                    if (el.is_array() && el.size() >= 2)
                        out.push_back({el[0].get<int>(), el[1].get<int>()});
                return out;
            };
            pa = new CreatePositionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("elementID").get<int>(), readPairs(params.at("startPairs")),
                readPairs(params.value("endPairs", json::array())),
                static_cast<uint16_t>(params.at("trackID").get<int>()));
            break;
        }
        case DeletePosition:
            pa = new DeletePositionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("elementID").get<int>(), params.at("positionID").get<int>());
            break;
        case MoveElementPosition:
            pa = new MoveElementPositionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("elementID").get<int>(), params.at("positionID").get<int>(), params.at("before"), params.at("after"));
            break;
        default:
            throw std::runtime_error("runRegisteredAction: unsupported action");
    }
    newAction(pa);
    return true;
}

