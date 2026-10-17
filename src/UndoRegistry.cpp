#include "UndoManager.h"
#include "JsonBytes.h"
#include "AutomationCurve.h"
#include "nodes/parametriceq/parametriceq.h"
#include "nodes/paramnode/paramnode.h"
#include "nodes/sampler/sampler.h"
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

namespace {

/// Merges caller-supplied fields into a live JSON snapshot, so a scripted edit
/// supplies intent ("pitch/rhythm/tuning change to X") rather than having to
/// echo back state it read a moment earlier.
void applyFields(json& obj, const json& intent, const char* const* fields, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (intent.contains(fields[i])) obj[fields[i]] = intent[fields[i]];
}

void applyNoteIntent(json& note, const json& intent) {
    static const char* const kFields[] = {
        "channel", "tuningMode", "tuningAnchorHarmonic", "tuningEdoSubdivisionSteps",
        "tuningEdoLowerVector", "tuningEdoUpperVector", "pitchVector",
        "rhythmVector", "rhythmEndVector", "rhythmEdoSubdivisionSteps",
        "rhythmEdoLowerVector", "rhythmEdoUpperVector",
    };
    applyFields(note, intent, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

void applyNoteTuningIntent(json& note, const json& intent) {
    static const char* const kFields[] = {
        "tuningMode", "tuningAnchorHarmonic", "tuningEdoSubdivisionSteps",
        "tuningEdoLowerVector", "tuningEdoUpperVector",
    };
    applyFields(note, intent, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

void applyRegionTuningIntent(json& region, const json& intent) {
    static const char* const kFields[] = {
        "tuningMode", "tuningAnchorHarmonic", "tuningEdoSubdivisionSteps",
        "tuningEdoLowerVector", "tuningEdoUpperVector", "tuningHarmonicAnchorVector",
        "rhythmEdoSubdivisionSteps", "rhythmEdoLowerVector", "rhythmEdoUpperVector",
    };
    applyFields(region, intent, kFields, sizeof(kFields) / sizeof(kFields[0]));
}

/// Reads a note's live snapshot, plus the index it occupies in its region.
json liveNoteJson(Project* proj, const std::vector<int>& managerPath, int nodeID, int regionID,
                  int noteID, size_t* outIndex) {
    Region& region = *undoResolveArrangerRegion(proj, managerPath, nodeID, regionID);
    auto it = region.id_to_index.find(noteID);
    if (it == region.id_to_index.end())
        throw std::runtime_error("note " + std::to_string(noteID) + " not found in region");
    const size_t idx = static_cast<size_t>(it->second);
    if (idx >= region.notes.size() || !region.notes[idx])
        throw std::runtime_error("note " + std::to_string(noteID) + " has no live object");
    if (outIndex) *outIndex = idx;
    return region.notes[idx]->toJSON();
}

json tempoPointsToJson(const AutomationCurve& c) {
    json pts = json::array();
    for (const auto& pt : c.points) {
        json jp;
        jp["timeVec"] = json::array();
        for (const auto& pr : pt.timeVec)
            jp["timeVec"].push_back(json::array({pr.first, pr.second}));
        jp["v"] = pt.v;
        jp["shape"] = static_cast<int>(pt.shape.type);
        jp["shapeParam"] = pt.shape.param;
        pts.push_back(std::move(jp));
    }
    return pts;
}

std::vector<std::pair<int, int>> readPairs(const json& j) {
    std::vector<std::pair<int, int>> out;
    if (!j.is_array()) return out;
    for (const auto& el : j)
        if (el.is_array() && el.size() >= 2)
            out.push_back({el[0].get<int>(), el[1].get<int>()});
    return out;
}

/// Copies the surrounding scale onto a freshly created note, mirroring what
/// PianoRoll::stampNoteTuning does on the mouse path, and persists the result on
/// the action so redo re-applies it. Per-note tuning fields in `params` override
/// the region's.
void stampCreatedNoteTuning(Project* proj, CreateNoteAction* cn, const json& params) {
    Region* region = undoResolveArrangerRegion(proj, cn->managerPath, cn->nodeID, cn->regionID);
    if (!region) return;
    auto it = region->id_to_index.find(cn->noteID);
    if (it == region->id_to_index.end()) return;
    const size_t idx = static_cast<size_t>(it->second);
    if (idx >= region->notes.size() || !region->notes[idx]) return;

    Note& n = *region->notes[idx];
    n.tuningMode = region->tuningMode;
    n.tuningAnchorHarmonic = region->tuningAnchorHarmonic;
    n.tuningEdoSubdivisionSteps = region->tuningEdoSubdivisionSteps;
    n.tuningEdoLowerVector = region->tuningEdoLowerVector;
    n.tuningEdoUpperVector = region->tuningEdoUpperVector;
    // The rhythm anchor records the grid this note was placed on, exactly as the
    // piano roll stamps it (PianoRollInput.cpp:902-904). Without it the note
    // carries the default grid and "recall note rhythm" recalls nothing.
    n.rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
    n.rhythmEdoLowerVector = region->rhythmEdoLowerVector;
    n.rhythmEdoUpperVector = region->rhythmEdoUpperVector;

    const json& intent = params.contains("intent") ? params["intent"] : params;
    if (intent.contains("rhythmEdoSubdivisionSteps")) n.rhythmEdoSubdivisionSteps = intent["rhythmEdoSubdivisionSteps"].get<int>();
    if (intent.contains("rhythmEdoLowerVector")) n.rhythmEdoLowerVector = readPairs(intent["rhythmEdoLowerVector"]);
    if (intent.contains("rhythmEdoUpperVector")) n.rhythmEdoUpperVector = readPairs(intent["rhythmEdoUpperVector"]);
    if (intent.contains("tuningMode")) n.tuningMode = intent["tuningMode"].get<int>();
    if (intent.contains("tuningAnchorHarmonic")) n.tuningAnchorHarmonic = intent["tuningAnchorHarmonic"].get<int>();
    if (intent.contains("tuningEdoSubdivisionSteps")) n.tuningEdoSubdivisionSteps = intent["tuningEdoSubdivisionSteps"].get<int>();
    if (intent.contains("tuningEdoLowerVector")) n.tuningEdoLowerVector = readPairs(intent["tuningEdoLowerVector"]);
    if (intent.contains("tuningEdoUpperVector")) n.tuningEdoUpperVector = readPairs(intent["tuningEdoUpperVector"]);

    n.syncNumFromPitchVector();
    cn->noteStampedSnapshot = n.toJSON();
}

} // namespace

const std::unordered_map<std::string, ActionType>& UndoManager::actionRegistry() {
    static const std::unordered_map<std::string, ActionType> reg = {
        {"create_note", CreateNote},
        {"add_arranger_track", AddArrangerTrack},
        {"add_node", AddNode},
        {"remove_node", RemoveNode},
        {"make_node_connection", MakeNodeConnection},
        {"sever_node_connection", SeverNodeConnection},
        {"reassign_node_connection", ReassignNodeConnection},
        {"remove_arranger_track", RemoveArrangerTrack},
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
        {"set_param_value", SetParamValue},
        {"move_node", MoveNode},
        {"delete_note", DeleteNote},
        {"move_note", MoveNote},
        {"move_notes", MoveMultipleNotes},
        {"toggle_node_visible", ToggleNodeVisible},
        {"pan_nodes", PanNodes},
        {"zoom_nodes", ZoomNodes},
        {"io_port_channel", IoPortChannel},
        {"songroll_rhythm_edo", SongRollRhythmEdo},
        {"piano_roll_region_tuning", PianoRollRegionTuning},
        {"assign_note_harmonic", AssignNoteHarmonic},
        {"sampler_add_sample", SamplerAddSample},
        {"sampler_remove_sample", SamplerRemoveSample},
        {"sampler_reorder_samples", SamplerReorderSamples},
        {"sampler_load_sample_file", SamplerLoadSampleFile},
        {"sampler_toggle_sample_solo", SamplerToggleSampleSolo},
        {"sampler_toggle_sample_mute", SamplerToggleSampleMute},
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
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"pitchVector":[[int,int],...],"rhythmVector":[[int,int],...],"rhythmEndVector":[[int,int],...]})";
    }
    if (actionName == "create_region") {
        return R"({"managerPath":[int,...],"nodeID":int})";
    }
    if (actionName == "delete_region") {
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int})";
    }
    if (actionName == "create_position") {
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"startPairs":[[int,int],...],"endPairs":[[int,int],...],"trackID":int})";
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
    if (actionName == "set_param_value")
        return R"({"managerPath":[int,...],"nodeID":int,"paramIndex":int,"value":float})";
    if (actionName == "delete_note")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"noteID":int})";
    // Note edits take intent: any subset of pitchVector/rhythmVector/rhythmEndVector/channel/
    // tuning*/rhythmEdo* — the "before" snapshot is read live from the note.
    if (actionName == "move_note")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"noteID":int,"intent":{...note fields...}})";
    if (actionName == "move_notes")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"notes":[{"noteID":int,"intent":{...}},...]})";
    if (actionName == "toggle_node_visible")
        return R"({"managerPath":[int,...],"nodeID":int})";
    if (actionName == "pan_nodes")
        return R"({"managerPath":[int,...],"dx":float,"dy":float})";
    if (actionName == "zoom_nodes")
        return R"({"managerPath":[int,...],"amount":float,"mx":float?,"my":float?})";
    if (actionName == "io_port_channel")
        return R"({"managerPath":[int,...],"op":int,"connectionId":int?,"connectionIndex":int?})";
    if (actionName == "create_automation_curve")
        return R"({"managerPath":[int,...],"nodeID":int})";
    // `before` is read live from the curve; supply the new point list.
    if (actionName == "modify_curve_points")
        return R"({"managerPath":[int,...],"nodeID":int,"curveID":int,"points":[object,...]})";
    if (actionName == "create_audio_clip")
        return R"({"managerPath":[int,...],"nodeID":int,"filepath":string})";
    if (actionName == "tempo_curve_edit")
        return R"({"before":[object,...],"after":[object,...]})";
    // `before` is read live from the arranger node; supply the new value as `after`.
    if (actionName == "songroll_rhythm_edo")
        return R"({"managerPath":[int,...],"nodeID":int,"after":{"steps":int,"lower":[[int,int],...],"upper":[[int,int],...]}}})";
    if (actionName == "map_parameter" || actionName == "unmap_parameter")
        return R"({"managerPath":[int,...],"nodeID":int,"paramIndex":int,"vstParamID":int?})";
    if (actionName == "paramnode_add_mod_row")
        return R"({"managerPath":[int,...],"nodeID":int})";
    if (actionName == "paramnode_remove_mod_row")
        return R"({"managerPath":[int,...],"nodeID":int,"modIndex":int})";
    if (actionName == "paramnode_toggle_centered")
        return R"({"managerPath":[int,...],"nodeID":int,"modIndex":int,"newCentered":bool?,"newDepth":float?})";
    if (actionName == "sampler_add_sample")
        return R"({"managerPath":[int,...],"nodeID":int,"filepath":string?})";
    if (actionName == "sampler_remove_sample")
        return R"({"managerPath":[int,...],"nodeID":int,"sampleIndex":int})";
    if (actionName == "sampler_reorder_samples")
        return R"({"managerPath":[int,...],"nodeID":int,"newOrder":[int,...]})";
    if (actionName == "sampler_load_sample_file")
        return R"({"managerPath":[int,...],"nodeID":int,"sampleIndex":int,"filepath":string})";
    if (actionName == "sampler_toggle_sample_solo" || actionName == "sampler_toggle_sample_mute")
        return R"({"managerPath":[int,...],"nodeID":int,"sampleIndex":int})";
    // Tuning edits take intent (a subset of the region/note tuning fields); the
    // "before" snapshots are read live.
    if (actionName == "piano_roll_region_tuning")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"intent":{...tuning fields...},"name":string?})";
    if (actionName == "assign_note_harmonic")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"noteID":int,"intent":{...tuning fields...}})";
    if (actionName == "add_eq_band")
        return R"({"managerPath":[int,...],"nodeID":int,"bandIndex":int?,"type":int?,"freq":float?,"gain":float?,"q":float?,"state":object?})";
    if (actionName == "remove_eq_band")
        return R"({"managerPath":[int,...],"nodeID":int,"bandIndex":int})";
    if (actionName == "vst_param_change")
        return R"({"managerPath":[int,...],"nodeID":int,"paramID":int,"value":float})";
    // oldState is read live from the plugin; supply the new blobs.
    if (actionName == "vst_state_change")
        return R"({"managerPath":[int,...],"nodeID":int,"compState":bytes?,"ctrlState":bytes?})";
    if (actionName == "vst_load_plugin")
        return R"({"managerPath":[int,...],"nodeID":int,"path":string})";
    if (actionName == "toggle_piano_roll_window")
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"open":bool?})";
    // Replaces whatever is on the two ports; the replaced cables are read live.
    if (actionName == "reassign_node_connection")
        return R"({"managerPath":[int,...],"srcNodeID":int,"srcConID":int,"dstNodeID":int,"dstConID":int})";
    if (actionName == "remove_arranger_track")
        return R"({"managerPath":[int,...],"nodeID":int,"trackType":int,"trackID":int,"connectionID":int})";
    return ""; // unknown action name: no schema (callers treat "" as "not described")
}

ProjectAction* UndoManager::buildRegisteredAction(const std::string& actionName, const json& params) {
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
            // Intent: "connect src→dst, replacing whatever is on those ports". The
            // cables being replaced are read live from the two ports (at most one
            // each, since a port holds a single connection).
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int sN = params.at("srcNodeID").get<int>();
            const int sC = params.at("srcConID").get<int>();
            const int dN = params.at("dstNodeID").get<int>();
            const int dC = params.at("dstConID").get<int>();
            Node* srcNode = nm.getNode(static_cast<uint16_t>(sN));
            Node* dstNode = nm.getNode(static_cast<uint16_t>(dN));
            if (!srcNode || !dstNode)
                throw std::runtime_error("reassign_node_connection: src/dst node not found");
            Connection* srcCon = srcNode->outputs.getConnection(static_cast<uint16_t>(sC));
            Connection* dstCon = dstNode->inputs.getConnection(static_cast<uint16_t>(dC));
            if (!srcCon || !dstCon)
                throw std::runtime_error("reassign_node_connection: src/dst port not found");

            ConnIDs olds[2];
            int oldCount = 0;
            if (dstCon->is_connected) {
                olds[oldCount].srcNodeID = dstCon->input_node;
                olds[oldCount].srcConID = dstCon->input_connection;
                olds[oldCount].dstNodeID = dstNode->id;
                olds[oldCount].dstConID = dstCon->id;
                olds[oldCount].existed = true;
                ++oldCount;
            }
            if (srcCon->is_connected) {
                olds[oldCount].srcNodeID = srcNode->id;
                olds[oldCount].srcConID = srcCon->id;
                olds[oldCount].dstNodeID = srcCon->output_node;
                olds[oldCount].dstConID = srcCon->output_connection;
                olds[oldCount].existed = true;
                ++oldCount;
            }
            if (oldCount == 0)
                throw std::runtime_error(
                    "reassign_node_connection: neither port is connected — use make_node_connection");
            pa = new ReassignNodeConnectionAction(head->p, std::move(mp), olds, oldCount, sN, sC, dN, dC);
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
        case SetParamValue: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            Node* target = (nodeID == 0) ? static_cast<Node*>(nm.outNode)
                         : (nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                         : nm.getNode(static_cast<uint16_t>(nodeID));
            if (!target) throw std::runtime_error("set_param_value: node not found");
            const size_t idx = static_cast<size_t>(params.value("paramIndex", 0));
            if (idx >= target->params.size())
                throw std::runtime_error("set_param_value: paramIndex out of range");
            // Old value is read live — the caller supplies intent, not state.
            pa = new SetParamValueUndoAction(head->p, std::move(mp), nodeID, { idx },
                target->params[idx]->value, params.at("value").get<float>());
            break;
        }
        case MoveNode: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            Node* n = nm.getNode(static_cast<uint16_t>(nodeID));
            if (!n) throw std::runtime_error("move_node: node not found");
            pa = new MoveNodeAction(head->p, std::move(mp), nodeID, n->dstRect.x, n->dstRect.y,
                                    params.at("toX").get<float>(), params.at("toY").get<float>());
            break;
        }
        case ToggleNodeVisible:
            pa = new ToggleNodeVisibleAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                                             params.at("nodeID").get<int>());
            break;
        case PanNodes:
            pa = new PanNodesAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                                    params.at("dx").get<float>(), params.at("dy").get<float>());
            break;
        case ZoomNodes:
            pa = new ZoomNodesAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                                     params.at("amount").get<float>(),
                                     params.value("mx", 0.0f), params.value("my", 0.0f));
            break;
        case IoPortChannel:
            pa = new IoPortChannelAction(head->p, params.at("op").get<int>(),
                params.at("managerPath").get<std::vector<int>>(),
                static_cast<uint16_t>(params.value("connectionId", 0)),
                static_cast<size_t>(params.value("connectionIndex", 0)));
            break;
        case DeleteNote:
            pa = new DeleteNoteAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), params.at("regionID").get<int>(), params.at("noteID").get<int>());
            break;
        case MoveNote: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int regionID = params.at("regionID").get<int>();
            const int noteID = params.at("noteID").get<int>();
            // before is read from the live note; the caller only supplies the change.
            json before = liveNoteJson(head->p, mp, nodeID, regionID, noteID, nullptr);
            json after = before;
            applyNoteIntent(after, params.contains("intent") ? params["intent"] : params);
            if (before == after) throw std::runtime_error("move_note: nothing to change");
            pa = new MoveNoteAction(head->p, std::move(mp), nodeID, regionID, noteID,
                std::move(before), std::move(after));
            break;
        }
        case MoveMultipleNotes: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int regionID = params.at("regionID").get<int>();
            std::vector<int> ids;
            std::vector<json> befores, afters;
            for (const auto& e : params.at("notes")) {
                const int noteID = e.at("noteID").get<int>();
                json before = liveNoteJson(head->p, mp, nodeID, regionID, noteID, nullptr);
                json after = before;
                applyNoteIntent(after, e.contains("intent") ? e["intent"] : e);
                ids.push_back(noteID);
                befores.push_back(std::move(before));
                afters.push_back(std::move(after));
            }
            if (ids.empty()) throw std::runtime_error("move_notes: no notes given");
            pa = new MoveMultipleNotesAction(head->p, std::move(mp), nodeID, regionID,
                std::move(ids), std::move(befores), std::move(afters));
            break;
        }
        case PianoRollRegionTuning: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int regionID = params.at("regionID").get<int>();
            Region& r = *undoResolveArrangerRegion(head->p, mp, nodeID, regionID);
            json before = r.toJSON();
            json after = before;
            applyRegionTuningIntent(after, params.contains("intent") ? params["intent"] : params);
            pa = new PianoRollRegionTuningUndoAction(head->p, std::move(mp), nodeID, regionID,
                std::move(before), std::move(after),
                params.value("name", std::string("Region Tuning")));
            break;
        }
        case AssignNoteHarmonic: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int regionID = params.at("regionID").get<int>();
            const int noteID = params.at("noteID").get<int>();
            Region& r = *undoResolveArrangerRegion(head->p, mp, nodeID, regionID);
            const json intent = params.contains("intent") ? params["intent"] : params;
            json beforeRegion = r.toJSON();
            json afterRegion = beforeRegion;
            applyRegionTuningIntent(afterRegion, intent);
            json beforeNote = liveNoteJson(head->p, mp, nodeID, regionID, noteID, nullptr);
            json afterNote = beforeNote;
            applyNoteTuningIntent(afterNote, intent);
            pa = new AssignNoteHarmonicUndoAction(head->p, std::move(mp), nodeID, regionID, noteID,
                std::move(beforeRegion), std::move(afterRegion),
                std::move(beforeNote), std::move(afterNote));
            break;
        }
        case AddEQBand: {
            json state = params.value("state", json::object());
            if (!state.contains("type")) state["type"] = params.value("type", 0);
            if (!state.contains("freq")) state["freq"] = params.value("freq", 1000.0f);
            if (!state.contains("gain")) state["gain"] = params.value("gain", 0.0f);
            if (!state.contains("q"))    state["q"] = params.value("q", 0.7f);
            pa = new AddEQBandAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), params.value("bandIndex", -1), std::move(state));
            break;
        }
        case RemoveEQBand: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* eq = dynamic_cast<ParametricEQNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!eq) throw std::runtime_error("remove_eq_band: node is not a ParametricEQ");
            const int bi = params.at("bandIndex").get<int>();
            if (bi < 0 || static_cast<size_t>(bi) >= eq->bands.size())
                throw std::runtime_error("remove_eq_band: bandIndex out of range");
            // Snapshot the band that is about to be removed, straight from live state.
            auto& band = *eq->bands[static_cast<size_t>(bi)];
            json state;
            state["type"] = band.type.value;
            state["freq"] = band.frequency.value;
            state["gain"] = band.gain.value;
            state["q"] = band.q.value;
            pa = new RemoveEQBandAction(head->p, std::move(mp), nodeID, bi, std::move(state));
            break;
        }
        case VstParameterChange: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!vst || !vst->plugin)
                throw std::runtime_error("vst_param_change: node has no loaded plugin");
            const int paramID = params.at("paramID").get<int>();
            // Old value read live from the plugin, exactly like the IComponentHandler path.
            const float oldValue = vst->plugin->getParameterValue(paramID);
            pa = new VstParameterChangeAction(head->p, std::move(mp), nodeID,
                static_cast<uint32_t>(paramID), oldValue, params.at("value").get<float>());
            break;
        }
        case VstStateChange: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!vst || !vst->plugin)
                throw std::runtime_error("vst_state_change: node has no loaded plugin");
            // Old state is the plugin's state right now; the caller supplies the new blobs.
            json oldState = json::object();
            oldState["compState"] = jsonBytesEncode(vst->plugin->getComponentState());
            oldState["ctrlState"] = jsonBytesEncode(vst->plugin->getControllerState());
            json newState = json::object();
            newState["compState"] = params.value("compState", oldState["compState"]);
            newState["ctrlState"] = params.value("ctrlState", oldState["ctrlState"]);
            pa = new VstStateChangeAction(head->p, std::move(mp), nodeID,
                std::move(oldState), std::move(newState));
            break;
        }
        case CreateAutomationCurve:
            pa = new CreateAutomationCurveAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>());
            break;
        case ModifyCurvePoints: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            const int curveID = params.at("curveID").get<int>();
            NodeManager& nm = requireManager(head->p, mp);
            auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!arr || !arr->sl || !arr->elements)
                throw std::runtime_error("modify_curve_points: arranger song roll unavailable");
            auto* curve = dynamic_cast<AutomationCurve*>(
                arr->elements->getElement(static_cast<uint16_t>(curveID)));
            if (!curve) throw std::runtime_error("modify_curve_points: curve not found");
            json before = arr->sl->snapshotCurvePoints(curve);
            json after = params.contains("points") ? params.at("points") : params.value("after", before);
            pa = new ModifyCurvePointsAction(head->p, std::move(mp), nodeID, curveID,
                std::move(before), std::move(after));
            break;
        }
        case CreateAudioClip:
            pa = new CreateAudioClipAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), params.at("filepath").get<std::string>());
            break;
        case TempoCurveEdit: {
            // Prior curve read live; the caller supplies only the new points.
            json before = tempoPointsToJson(head->p->tempoCurve);
            json after = params.contains("after") ? params.at("after") : before;
            pa = new TempoCurveEditAction(head->p, std::move(before), std::move(after));
            break;
        }
        case SongRollRhythmEdo: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            const int nodeID = params.at("nodeID").get<int>();
            NodeManager& nm = requireManager(head->p, mp);
            auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!arr) throw std::runtime_error("songroll_rhythm_edo: node is not an Arranger");
            auto pairs = [](const std::vector<std::pair<int, int>>& v) {
                json a = json::array();
                for (const auto& pr : v) a.push_back(json::array({pr.first, pr.second}));
                return a;
            };
            json before;
            before["steps"] = arr->rhythmEdoSubdivisionSteps;
            before["lower"] = pairs(arr->rhythmEdoLowerVector);
            before["upper"] = pairs(arr->rhythmEdoUpperVector);
            json after = params.contains("after") ? params.at("after") : before;
            pa = new SongRollRhythmEdoAction(head->p, std::move(mp), nodeID,
                std::move(before), std::move(after));
            break;
        }
        case MapParameter:
            pa = new MapParameterUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.value("paramIndex", 0)),
                params.value("vstParamID", -1));
            break;
        case UnmapParameter:
            pa = new UnmapParameterUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.value("paramIndex", 0)),
                params.value("vstParamID", -1));
            break;
        case ParamNodeAddModRow:
            pa = new ParamNodeAddModRowUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>());
            break;
        case ParamNodeRemoveModRow:
            pa = new ParamNodeRemoveModRowUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.at("modIndex").get<int>()));
            break;
        case ParamNodeToggleCentered: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* pn = dynamic_cast<ParamNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!pn) throw std::runtime_error("paramnode_toggle_centered: node is not a Param node");
            const size_t mi = static_cast<size_t>(params.at("modIndex").get<int>());
            if (mi >= pn->modulators.size())
                throw std::runtime_error("paramnode_toggle_centered: modIndex out of range");
            Modulator* mod = pn->modulators[mi];
            if (!mod) throw std::runtime_error("paramnode_toggle_centered: modulator is null");
            const bool oldCentered = mod->centered;
            const float oldDepth = mod->depth.value;
            const bool newCentered = params.contains("newCentered") ? params["newCentered"].get<bool>() : !oldCentered;
            const float newDepth = params.value("newDepth", oldDepth);
            pa = new ParamNodeToggleCenteredUndoAction(head->p, std::move(mp), nodeID, mi,
                oldCentered, newCentered, oldDepth, newDepth);
            break;
        }
        case SamplerAddSample:
            pa = new SamplerAddSampleUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), params.value("filepath", std::string()));
            break;
        case SamplerRemoveSample:
            pa = new SamplerRemoveSampleUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.at("sampleIndex").get<int>()));
            break;
        case SamplerReorderSamples: {
            std::vector<size_t> order;
            for (const auto& v : params.at("newOrder")) order.push_back(static_cast<size_t>(v.get<int>()));
            pa = new SamplerReorderSamplesUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), std::move(order));
            break;
        }
        case SamplerLoadSampleFile: {
            auto mp = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, mp);
            const int nodeID = params.at("nodeID").get<int>();
            auto* sampler = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
            if (!sampler) throw std::runtime_error("sampler_load_sample_file: node is not a Sampler");
            const size_t si = static_cast<size_t>(params.at("sampleIndex").get<int>());
            if (si >= sampler->samples.size())
                throw std::runtime_error("sampler_load_sample_file: sampleIndex out of range");
            const auto& slab = sampler->samples[si];
            pa = new SamplerLoadSampleFileUndoAction(head->p, std::move(mp), nodeID, si,
                slab.filepath, params.at("filepath").get<std::string>(),
                slab.sampleRate, slab.numChannels, slab.sampleRate, slab.numChannels);
            break;
        }
        case SamplerToggleSampleSolo:
            pa = new SamplerToggleSampleSoloUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.at("sampleIndex").get<int>()));
            break;
        case SamplerToggleSampleMute:
            pa = new SamplerToggleSampleMuteUndoAction(head->p, params.at("managerPath").get<std::vector<int>>(),
                params.at("nodeID").get<int>(), static_cast<size_t>(params.at("sampleIndex").get<int>()));
            break;
        default:
            throw std::runtime_error("runRegisteredAction: unsupported action: " + actionName);
    }
    if (!pa)
        throw std::runtime_error("runRegisteredAction: action not constructed: " + actionName);
    return pa;
}

bool UndoManager::runRegisteredAction(const std::string& actionName, const json& params, std::string& error) {
    error.clear();
    try {
        // A few operations are only reachable through the node/song-roll method the
        // UI itself calls, because they build and run their own action internally.
        if (actionName == "vst_load_plugin" || actionName == "toggle_piano_roll_window") {
            return runDelegatedAction(actionName, params, error);
        }

        ProjectAction* pa = buildRegisteredAction(actionName, params);
        // The UI applies these mutations itself before recording the action, so they
        // ship with skipInitialDo set. A scripted caller has applied nothing, so run
        // doAction and let the action perform the change.
        pa->skipInitialDo = false;
        newAction(pa);

        // A note only means something inside the scale it was placed in. The piano
        // roll stamps the surrounding tuning onto each note it creates; a scripted
        // create must do the same, or the note carries bare ratios with no scale.
        // `current == pa` guards the coalesce path, which deletes the action.
        if (pa->type == CreateNote && current == pa)
            stampCreatedNoteTuning(head->p, static_cast<CreateNoteAction*>(pa), params);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown error";
        return false;
    }
}

bool UndoManager::runDelegatedAction(const std::string& actionName, const json& params, std::string& error) {
    try {
        const auto managerPath = params.at("managerPath").get<std::vector<int>>();
        NodeManager& nm = requireManager(head->p, managerPath);
        const int nodeID = params.at("nodeID").get<int>();
        Node* node = nm.getNode(static_cast<uint16_t>(nodeID));
        if (!node) { error = actionName + ": node not found"; return false; }

        if (actionName == "vst_load_plugin") {
            auto* vst = dynamic_cast<VstNode*>(node);
            if (!vst) { error = "vst_load_plugin: node is not a VST node"; return false; }
            vst->loadPlugin(params.at("path").get<std::string>(), /*createUndo=*/true);
            return true;
        }

        // toggle_piano_roll_window
        auto* arr = dynamic_cast<ArrangerNode*>(node);
        if (!arr) { error = "toggle_piano_roll_window: node is not an Arranger node"; return false; }
        arr->ensureSongRoll();
        if (!arr->sl || !arr->elements) { error = "toggle_piano_roll_window: arranger has no song roll"; return false; }
        const int regionID = params.at("regionID").get<int>();
        if (!params.value("open", true)) {
            arr->sl->clearPianoRoll(regionID, /*createUndo=*/true);
            return true;
        }
        auto* region = dynamic_cast<Region*>(arr->elements->getElement(static_cast<uint16_t>(regionID)));
        if (!region) { error = "toggle_piano_roll_window: region not found"; return false; }
        arr->sl->createPianoRoll(region, /*createUndo=*/true);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

