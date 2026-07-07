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

// ProjectAction JSON (de)serialization (split from UndoManager.cpp).

ProjectAction* ProjectAction::deSerialize(json j, Project* p) {
    ProjectAction* pa;
    switch (j.at("type").get<int>()) {
        case CreateNote: {
            auto managerPath = j.at("managerPath").get<std::vector<int>>();
            std::vector<std::pair<int, int>> pairs;
            for (const auto& el : j.at("pitchVector")) {
                pairs.push_back({el.at(0).get<int>(), el.at(1).get<int>()});
            }
            std::vector<std::pair<int,int>> rhythmPairs;
            if (j.contains("rhythmVector") && j["rhythmVector"].is_array()) {
                for (const auto& el : j["rhythmVector"])
                    if (el.is_array() && el.size() >= 2)
                        rhythmPairs.push_back({el[0].get<int>(), el[1].get<int>()});
            }
            std::vector<std::pair<int,int>> endRPairs;
            if (j.contains("rhythmEndVector") && j["rhythmEndVector"].is_array()) {
                for (const auto& el : j["rhythmEndVector"])
                    if (el.is_array() && el.size() >= 2)
                        endRPairs.push_back({el[0].get<int>(), el[1].get<int>()});
            }
            if (endRPairs.empty()) endRPairs = rhythmPairs;
            auto cn = new CreateNoteAction(p, managerPath, j.at("nodeID").get<int>(), j.at("regionID").get<int>(), std::move(rhythmPairs),
                std::move(endRPairs), std::move(pairs));
            cn->noteID = j.at("noteID").get<int>();
            if (j.contains("noteStampedSnapshot") && !j["noteStampedSnapshot"].is_null())
                cn->noteStampedSnapshot = j["noteStampedSnapshot"];
            pa = cn;
            break;
        }
        case AddArrangerTrack: {
            auto at = new AddArrangerTrackAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("trackType").get<int>());
            at->trackID = j.at("trackID").get<int>();
            at->connectionID = j.at("connectionID").get<int>();
            pa = at;
            break;
        }
        case RemoveArrangerTrack: {
            auto rt = new RemoveArrangerTrackAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("trackType").get<int>(), j.at("trackID").get<int>(), j.at("connectionID").get<int>());
            if (j.contains("trackIndex"))
                rt->trackIndex = j["trackIndex"].get<int>();
            if (j.contains("positionsSnapshot"))
                rt->positionsSnapshot = j["positionsSnapshot"];
            if (j.contains("trackIdPoolSnapshot"))
                rt->trackIdPoolSnapshot = j["trackIdPoolSnapshot"];
            if (j.contains("connectionIdPoolSnapshot"))
                rt->connectionIdPoolSnapshot = j["connectionIdPoolSnapshot"];
            if (j.contains("positionIdPoolSnapshot"))
                rt->positionIdPoolSnapshot = j["positionIdPoolSnapshot"];
            pa = rt;
            break;
        }
        case AddNode: {
            auto an = new AddNodeAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeType").get<int>(), j.at("x").get<float>(), j.at("y").get<float>());
            an->nodeID = j.at("nodeID").get<int>();
            if (j.contains("redoNodeSnapshot")) {
                an->hasRedoRestore = true;
                an->redoNodeSnapshot = j.at("redoNodeSnapshot");
                an->redoConnectionsSnapshot = j.value("redoConnectionsSnapshot", json::array());
            }
            an->panOffX = j.value("panOffX", 0.f);
            an->panOffY = j.value("panOffY", 0.f);
            an->patcherData = j.value("patcherData", json());
            pa = an;
            break;
        }
        case MoveEmbeddedWindow: {
            pa = new MoveEmbeddedWindowAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("ewID").get<int>(),
                j.at("fromX").get<float>(), j.at("fromY").get<float>(),
                j.at("toX").get<float>(), j.at("toY").get<float>());
            break;
        }
        case ResizeEmbeddedWindow: {
            pa = new ResizeEmbeddedWindowAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("ewID").get<int>(),
                j.at("fromX").get<float>(), j.at("fromY").get<float>(),
                j.at("fromW").get<float>(), j.at("fromH").get<float>(),
                j.at("toX").get<float>(), j.at("toY").get<float>(),
                j.at("toW").get<float>(), j.at("toH").get<float>());
            break;
        }
        case ToggleNodeVisible: {
            pa = new ToggleNodeVisibleAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeId").get<int>());
            break;
        }
        case PanNodes: {
            pa = new PanNodesAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("dx").get<float>(), j.at("dy").get<float>());
            break;
        }
        case ZoomNodes: {
            auto* zn = new ZoomNodesAction(p, j.at("managerPath").get<std::vector<int>>(),
                j.at("amounts")[0].get<float>(), j.at("mxs")[0].get<float>(), j.at("mys")[0].get<float>());
            for (size_t i = 1; i < j.at("amounts").size(); ++i)
                zn->addStep(j.at("amounts")[i].get<float>(), j.at("mxs")[i].get<float>(), j.at("mys")[i].get<float>());
            pa = zn;
            break;
        }
        case RemoveNode: {
            auto rn = new RemoveNodeAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>());
            rn->nodeData = j.at("nodeData");
            rn->connectionsData = j.at("connectionsData");
            rn->panOffX = j.value("panOffX", 0.f);
            rn->panOffY = j.value("panOffY", 0.f);
            rn->patcherData = j.value("patcherData", json());
            pa = rn;
            break;
        }
        case MakeNodeConnection: {
            pa = new MakeNodeConnectionAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("srcNodeID").get<int>(), j.at("srcConID").get<int>(),
                j.at("dstNodeID").get<int>(), j.at("dstConID").get<int>());
            break;
        }
        case SeverNodeConnection: {
            pa = new SeverNodeConnectionAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("srcNodeID").get<int>(), j.at("srcConID").get<int>(),
                j.at("dstNodeID").get<int>(), j.at("dstConID").get<int>());
            break;
        }
        case ReassignNodeConnection: {
            auto mp = j.at("managerPath").get<std::vector<int>>();
            int oldCount = j.at("oldConnCount").get<int>();
            ConnIDs oldCxns[2];
            if (j.contains("oldConns")) {
                int i = 0;
                for (auto& oc : j.at("oldConns")) {
                    if (i >= 2) break;
                    oldCxns[i].srcNodeID = oc.at("srcNodeID").get<int>();
                    oldCxns[i].srcConID = oc.at("srcConID").get<int>();
                    oldCxns[i].dstNodeID = oc.at("dstNodeID").get<int>();
                    oldCxns[i].dstConID = oc.at("dstConID").get<int>();
                    oldCxns[i].existed = oc.at("existed").get<bool>();
                    ++i;
                }
            }
            pa = new ReassignNodeConnectionAction(p, std::move(mp), oldCxns, oldCount,
                j.at("newSrcNodeID").get<int>(), j.at("newSrcConID").get<int>(),
                j.at("newDstNodeID").get<int>(), j.at("newDstConID").get<int>());
            break;
        }
        case MoveNote: {
            pa = new MoveNoteAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("regionID").get<int>(), j.at("noteID").get<int>(),
                j.at("before"), j.at("after"));
            break;
        }
        case MoveMultipleNotes: {
            pa = new MoveMultipleNotesAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("regionID").get<int>(),
                j.at("noteIDs").get<std::vector<int>>(), j.at("befores").get<std::vector<json>>(), j.at("afters").get<std::vector<json>>(),
                j.value("name", "Move Notes"));
            break;
        }
        case DeleteNote: {
            pa = new DeleteNoteAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("regionID").get<int>(), j.at("noteID").get<int>(),
                j.at("insertIndex").get<size_t>(), j.at("noteSnapshot"));
            break;
        }
        case UndoHead: {
            (void)j.at("managerPath");
            pa = new UndoHeadAction(p);
            break;
        }
        case PianoRollRegionTuning: {
            pa = new PianoRollRegionTuningUndoAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("regionID").get<int>(),
                j.at("beforeRegion"), j.at("afterRegion"), j.at("name").get<std::string>());
            break;
        }
        case AssignNoteHarmonic: {
            pa = new AssignNoteHarmonicUndoAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("regionID").get<int>(), j.at("noteID").get<int>(),
                j.at("beforeRegion"), j.at("afterRegion"), j.at("beforeNote"), j.at("afterNote"));
            break;
        }
        case AddModSourceUndo: {
            pa = new AddModSourceUndoAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("paramPath").get<std::vector<size_t>>());
            break;
        }
        case RemoveModSourceUndo: {
            pa = new RemoveModSourceUndoAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("paramPath").get<std::vector<size_t>>(),
                j.at("modIndex").get<size_t>());
            break;
        }
        case CreateRegion: {
            const auto managerPath = j.at("managerPath").get<std::vector<int>>();
            const int nodeID = j.at("nodeID").get<int>();
            if (j.value("snapshotValid", false))
                pa = new CreateRegionAction(p, managerPath, nodeID, j.at("regionID").get<int>(), j.at("regionSnapshot"));
            else
                pa = new CreateRegionAction(p, managerPath, nodeID);
            break;
        }
        case DeleteRegion:
            pa = new DeleteRegionAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("regionID").get<int>(),
                j.at("elementInsertIndex").get<size_t>(), j.at("regionSnapshot"));
            break;
        case CreatePosition: {
            const auto managerPath = j.at("managerPath").get<std::vector<int>>();
            auto readPairs = [](const json& j) {
                std::vector<std::pair<int,int>> out;
                for (const auto& el : j)
                    if (el.is_array() && el.size() >= 2)
                        out.push_back({el[0].get<int>(), el[1].get<int>()});
                return out;
            };
            auto cpp = new CreatePositionAction(p, managerPath, j.at("nodeID").get<int>(), j.at("elementID").get<int>(),
                readPairs(j.at("startPairs")), readPairs(j.value("endPairs", json::array())),
                static_cast<uint16_t>(j.at("trackID").get<int>()));
            cpp->positionID = j.at("positionID").get<int>();
            pa = cpp;
            break;
        }
        case DeletePosition:
            pa = new DeletePositionAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("elementID").get<int>(),
                j.at("positionID").get<int>(), j.at("insertIndex").get<size_t>(), j.at("positionSnapshot"));
            break;
        case MoveElementPosition:
            pa = new MoveElementPositionAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("elementID").get<int>(),
                j.at("positionID").get<int>(), j.at("before"), j.at("after"));
            break;
        case IoPortChannel: {
            auto* io = new IoPortChannelAction(p, j.at("op").get<int>(), j.at("managerPath").get<std::vector<int>>(),
                static_cast<uint16_t>(j.at("connectionId").get<int>()), j.at("connectionIndex").get<size_t>());
            io->idAssigned = j.value("idAssigned", true);
            pa = io;
            break;
        }
        case SetParamValue: {
            pa = new SetParamValueUndoAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(),
                j.at("paramPath").get<std::vector<size_t>>(), j.at("oldValue").get<float>(), j.at("newValue").get<float>(),
                j.at("name").get<std::string>());
            break;
        }
        case ToggleModulatorCentered: {
            pa = new ToggleModulatorCenteredUndoAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(),
                j.at("paramPath").get<std::vector<size_t>>(), j.at("modIndex").get<size_t>(),
                j.at("oldCentered").get<bool>(), j.at("newCentered").get<bool>(),
                j.at("oldDepth").get<float>(), j.at("newDepth").get<float>());
            break;
        }
        case AddEQBand: {
            pa = new AddEQBandAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(),
                j.at("bandIndex").get<int>(), j.at("bandState"));
            break;
        }
        case RemoveEQBand: {
            pa = new RemoveEQBandAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(),
                j.at("bandIndex").get<int>(), j.at("bandState"));
            break;
        }
        case VstParameterChange: {
            pa = new VstParameterChangeAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(),
                j.at("paramID").get<uint32_t>(), j.at("oldValue").get<float>(), j.at("newValue").get<float>());
            break;
        }
        case VstLoadPlugin: {
            pa = new VstLoadPluginAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(),
                j.at("oldState"), j.at("newState"));
            break;
        }
        case TogglePianoRollWindow: {
            pa = new TogglePianoRollWindowAction(p, j.at("managerPath").get<std::vector<int>>(),
                j.at("arrangerNodeID").get<int>(), j.at("regionID").get<int>(), j.at("ewID").get<int>(),
                j.at("x").get<float>(), j.at("y").get<float>(), j.at("w").get<float>(), j.at("h").get<float>(),
                j.value("zOrder", 0), j.at("open").get<bool>());
            break;
        }
        case SongRollRhythmEdo:
            pa = new SongRollRhythmEdoAction(p, j.at("managerPath").get<std::vector<int>>(),
                j.at("nodeID").get<int>(), j.at("before"), j.at("after"));
            break;
        case CreateAutomationCurve:
            pa = new CreateAutomationCurveAction(p, j.at("managerPath").get<std::vector<int>>(),
                j.at("nodeID").get<int>(), j.at("curveID").get<int>(), j.at("curveSnapshot"));
            break;
        case CreateAudioClip:
            pa = new CreateAudioClipAction(p, j.at("managerPath").get<std::vector<int>>(),
                j.at("nodeID").get<int>(), j.at("clipID").get<int>(), j.at("clipSnapshot"));
            break;
        case ModifyCurvePoints:
            pa = new ModifyCurvePointsAction(p, j.at("managerPath").get<std::vector<int>>(),
                j.at("nodeID").get<int>(), j.at("curveID").get<int>(),
                j.at("before"), j.at("after"));
            break;
        default:
            throw std::runtime_error("invalid undo action type in save");
    }

    for (auto jc : j.at("children")) {
        auto c = ProjectAction::deSerialize(jc, p);
        pa->newAction(c);
    }

    pa->last_index = j.at("last_index").get<int>();
    pa->name = j.at("name").get<std::string>();
    pa->undoTreeExpanded = j.value("undoTreeExpanded", false);
    if (j.contains("savedMainManager"))
        pa->savedMainManager = j["savedMainManager"];
    return pa;
}

json ProjectAction::serialize(ProjectAction* pa) {
    json j;
    j["type"] = pa->type;
    switch (pa->type) {
        case CreateNote: {
            auto cn = static_cast<CreateNoteAction*>(pa);
            j["managerPath"] = cn->managerPath;
            j["nodeID"] = cn->nodeID;
            j["regionID"] = cn->regionID;
            j["rhythmVector"] = json::array();
            for (const auto& pr : cn->rhythmVector)
                j["rhythmVector"].push_back(json::array({pr.first, pr.second}));
            j["rhythmEndVector"] = json::array();
            for (const auto& pr : cn->rhythmEndVector)
                j["rhythmEndVector"].push_back(json::array({pr.first, pr.second}));
            j["pitchVector"] = json::array();
            for (const auto& pr : cn->pitchVector)
                j["pitchVector"].push_back(json::array({pr.first, pr.second}));
            j["noteID"] = cn->noteID;
            if (!cn->noteStampedSnapshot.is_null())
                j["noteStampedSnapshot"] = cn->noteStampedSnapshot;
            break;
        }
        case AddArrangerTrack: {
            auto at = static_cast<AddArrangerTrackAction*>(pa);
            j["managerPath"] = at->managerPath;
            j["nodeID"] = at->nodeID;
            j["trackType"] = at->trackType;
            j["trackID"] = at->trackID;
            j["connectionID"] = at->connectionID;
            break;
        }
        case RemoveArrangerTrack: {
            auto rt = static_cast<RemoveArrangerTrackAction*>(pa);
            j["managerPath"] = rt->managerPath;
            j["nodeID"] = rt->nodeID;
            j["trackType"] = rt->trackType;
            j["trackID"] = rt->trackID;
            j["connectionID"] = rt->connectionID;
            j["trackIndex"] = rt->trackIndex;
            j["positionsSnapshot"] = rt->positionsSnapshot;
            if (!rt->trackIdPoolSnapshot.is_null()) j["trackIdPoolSnapshot"] = rt->trackIdPoolSnapshot;
            if (!rt->connectionIdPoolSnapshot.is_null()) j["connectionIdPoolSnapshot"] = rt->connectionIdPoolSnapshot;
            if (!rt->positionIdPoolSnapshot.is_null()) j["positionIdPoolSnapshot"] = rt->positionIdPoolSnapshot;
            break;
        }
        case MoveEmbeddedWindow: {
            auto mw = static_cast<MoveEmbeddedWindowAction*>(pa);
            j["managerPath"] = mw->managerPath;
            j["ewID"] = mw->ewID;
            j["fromX"] = mw->fromX;
            j["fromY"] = mw->fromY;
            j["toX"] = mw->toX;
            j["toY"] = mw->toY;
            break;
        }
        case ResizeEmbeddedWindow: {
            auto rw = static_cast<ResizeEmbeddedWindowAction*>(pa);
            j["managerPath"] = rw->managerPath;
            j["ewID"] = rw->ewID;
            j["fromX"] = rw->fromX;
            j["fromY"] = rw->fromY;
            j["fromW"] = rw->fromW;
            j["fromH"] = rw->fromH;
            j["toX"] = rw->toX;
            j["toY"] = rw->toY;
            j["toW"] = rw->toW;
            j["toH"] = rw->toH;
            break;
        }
        case ToggleNodeVisible: {
            auto tv = static_cast<ToggleNodeVisibleAction*>(pa);
            j["managerPath"] = tv->managerPath;
            j["nodeId"] = tv->nodeId;
            break;
        }
        case PanNodes: {
            auto pn = static_cast<PanNodesAction*>(pa);
            j["managerPath"] = pn->managerPath;
            j["dx"] = pn->dx;
            j["dy"] = pn->dy;
            break;
        }
        case ZoomNodes: {
            auto zn = static_cast<ZoomNodesAction*>(pa);
            j["managerPath"] = zn->managerPath;
            j["amounts"] = zn->amounts;
            j["mxs"] = zn->mxs;
            j["mys"] = zn->mys;
            break;
        }
        case AddNode: {
            auto an = static_cast<AddNodeAction*>(pa);
            j["managerPath"] = an->managerPath;
            j["nodeType"] = an->nodeType;
            j["x"] = an->x;
            j["y"] = an->y;
            j["nodeID"] = an->nodeID;
            if (an->hasRedoRestore) {
                j["redoNodeSnapshot"] = an->redoNodeSnapshot;
                j["redoConnectionsSnapshot"] = an->redoConnectionsSnapshot;
            }
            j["panOffX"] = an->panOffX;
            j["panOffY"] = an->panOffY;
            if (!an->patcherData.is_null())
                j["patcherData"] = an->patcherData;
            break;
        }
        case RemoveNode: {
            auto rn = static_cast<RemoveNodeAction*>(pa);
            if (rn->nodeData.is_null()) {
                NodeManager& nm = requireManager(rn->p, rn->managerPath);
                (void)nm.snapshotNode(static_cast<uint16_t>(rn->nodeID), rn->nodeData, rn->connectionsData);
            }
            j["managerPath"] = rn->managerPath;
            j["nodeID"] = rn->nodeID;
            j["nodeData"] = rn->nodeData;
            j["connectionsData"] = rn->connectionsData;
            j["panOffX"] = rn->panOffX;
            j["panOffY"] = rn->panOffY;
            if (!rn->patcherData.is_null())
                j["patcherData"] = rn->patcherData;
            break;
        }
        case MakeNodeConnection: {
            auto mc = static_cast<MakeNodeConnectionAction*>(pa);
            j["managerPath"] = mc->managerPath;
            j["srcNodeID"] = mc->srcNodeID;
            j["srcConID"] = mc->srcConID;
            j["dstNodeID"] = mc->dstNodeID;
            j["dstConID"] = mc->dstConID;
            break;
        }
        case SeverNodeConnection: {
            auto sc = static_cast<SeverNodeConnectionAction*>(pa);
            j["managerPath"] = sc->managerPath;
            j["srcNodeID"] = sc->srcNodeID;
            j["srcConID"] = sc->srcConID;
            j["dstNodeID"] = sc->dstNodeID;
            j["dstConID"] = sc->dstConID;
            break;
        }
        case ReassignNodeConnection: {
            auto ra = static_cast<ReassignNodeConnectionAction*>(pa);
            j["managerPath"] = ra->managerPath;
            j["newSrcNodeID"] = ra->newSrcNodeID;
            j["newSrcConID"] = ra->newSrcConID;
            j["newDstNodeID"] = ra->newDstNodeID;
            j["newDstConID"] = ra->newDstConID;
            j["oldConnCount"] = ra->oldConnCount;
            j["oldConns"] = json::array();
            for (int i = 0; i < ra->oldConnCount; ++i) {
                json oc;
                oc["srcNodeID"] = ra->oldConns[i].srcNodeID;
                oc["srcConID"] = ra->oldConns[i].srcConID;
                oc["dstNodeID"] = ra->oldConns[i].dstNodeID;
                oc["dstConID"] = ra->oldConns[i].dstConID;
                oc["existed"] = ra->oldConns[i].existed;
                j["oldConns"].push_back(oc);
            }
            break;
        }
        case MoveNote: {
            auto mn = static_cast<MoveNoteAction*>(pa);
            j["managerPath"] = mn->managerPath;
            j["nodeID"] = mn->nodeID;
            j["regionID"] = mn->regionID;
            j["noteID"] = mn->noteID;
            j["before"] = mn->before;
            j["after"] = mn->after;
            break;
        }
        case MoveMultipleNotes: {
            auto mm = static_cast<MoveMultipleNotesAction*>(pa);
            j["managerPath"] = mm->managerPath;
            j["nodeID"] = mm->nodeID;
            j["regionID"] = mm->regionID;
            j["noteIDs"] = mm->noteIDs;
            j["befores"] = mm->befores;
            j["afters"] = mm->afters;
            break;
        }
        case DeleteNote: {
            auto dn = static_cast<DeleteNoteAction*>(pa);
            j["managerPath"] = dn->managerPath;
            j["nodeID"] = dn->nodeID;
            j["regionID"] = dn->regionID;
            j["noteID"] = dn->noteID;
            j["insertIndex"] = dn->insertIndex;
            j["noteSnapshot"] = dn->noteSnapshot;
            break;
        }
        case UndoHead:
            j["managerPath"] = json::array();
            break;
        case PianoRollRegionTuning: {
            auto pr = static_cast<PianoRollRegionTuningUndoAction*>(pa);
            j["managerPath"] = pr->managerPath;
            j["nodeID"] = pr->nodeID;
            j["regionID"] = pr->regionID;
            j["beforeRegion"] = pr->beforeRegion;
            j["afterRegion"] = pr->afterRegion;
            break;
        }
        case AssignNoteHarmonic: {
            auto ah = static_cast<AssignNoteHarmonicUndoAction*>(pa);
            j["managerPath"] = ah->managerPath;
            j["nodeID"] = ah->nodeID;
            j["regionID"] = ah->regionID;
            j["noteID"] = ah->noteID;
            j["beforeRegion"] = ah->beforeRegion;
            j["afterRegion"] = ah->afterRegion;
            j["beforeNote"] = ah->beforeNote;
            j["afterNote"] = ah->afterNote;
            break;
        }
        case AddModSourceUndo: {
            auto am = static_cast<AddModSourceUndoAction*>(pa);
            j["managerPath"] = am->managerPath;
            j["nodeID"] = am->nodeID;
            j["paramPath"] = am->paramPath;
            break;
        }
        case RemoveModSourceUndo: {
            auto rm = static_cast<RemoveModSourceUndoAction*>(pa);
            j["managerPath"] = rm->managerPath;
            j["nodeID"] = rm->nodeID;
            j["paramPath"] = rm->paramPath;
            j["modIndex"] = rm->modIndex;
            break;
        }
        case CreateRegion: {
            auto cr = static_cast<CreateRegionAction*>(pa);
            j["managerPath"] = cr->managerPath;
            j["nodeID"] = cr->nodeID;
            j["regionID"] = cr->regionID;
            j["regionSnapshot"] = cr->regionSnapshot;
            j["snapshotValid"] = cr->snapshotValid;
            break;
        }
        case DeleteRegion: {
            auto dr = static_cast<DeleteRegionAction*>(pa);
            j["managerPath"] = dr->managerPath;
            j["nodeID"] = dr->nodeID;
            j["regionID"] = dr->regionID;
            j["elementInsertIndex"] = dr->elementInsertIndex;
            j["regionSnapshot"] = dr->regionSnapshot;
            break;
        }
        case CreatePosition: {
            auto cp = static_cast<CreatePositionAction*>(pa);
            j["managerPath"] = cp->managerPath;
            j["nodeID"] = cp->nodeID;
            j["elementID"] = cp->elementID;
            j["startPairs"] = json::array();
            for (const auto& pr : cp->startPairs)
                j["startPairs"].push_back(json::array({pr.first, pr.second}));
            j["endPairs"] = json::array();
            for (const auto& pr : cp->endPairs)
                j["endPairs"].push_back(json::array({pr.first, pr.second}));
            j["trackID"] = cp->trackID;
            j["positionID"] = cp->positionID;
            break;
        }
        case DeletePosition: {
            auto dp = static_cast<DeletePositionAction*>(pa);
            j["managerPath"] = dp->managerPath;
            j["nodeID"] = dp->nodeID;
            j["elementID"] = dp->elementID;
            j["positionID"] = dp->positionID;
            j["insertIndex"] = dp->insertIndex;
            j["positionSnapshot"] = dp->positionSnapshot;
            break;
        }
        case MoveElementPosition: {
            auto mp = static_cast<MoveElementPositionAction*>(pa);
            j["managerPath"] = mp->managerPath;
            j["nodeID"] = mp->nodeID;
            j["elementID"] = mp->elementID;
            j["positionID"] = mp->positionID;
            j["before"] = mp->before;
            j["after"] = mp->after;
            break;
        }
        case IoPortChannel: {
            auto* io = static_cast<IoPortChannelAction*>(pa);
            j["managerPath"] = io->managerPath;
            j["op"] = io->op;
            j["connectionId"] = io->connectionId;
            j["connectionIndex"] = io->connectionIndex;
            j["idAssigned"] = io->idAssigned;
            break;
        }
        case SetParamValue: {
            auto* sv = static_cast<SetParamValueUndoAction*>(pa);
            j["managerPath"] = sv->managerPath;
            j["nodeID"] = sv->nodeID;
            j["paramPath"] = sv->paramPath;
            j["oldValue"] = sv->oldValue;
            j["newValue"] = sv->newValue;
            break;
        }
        case ToggleModulatorCentered: {
            auto* tc = static_cast<ToggleModulatorCenteredUndoAction*>(pa);
            j["managerPath"] = tc->managerPath;
            j["nodeID"] = tc->nodeID;
            j["paramPath"] = tc->paramPath;
            j["modIndex"] = tc->modIndex;
            j["oldCentered"] = tc->oldCentered;
            j["newCentered"] = tc->newCentered;
            j["oldDepth"] = tc->oldDepth;
            j["newDepth"] = tc->newDepth;
            break;
        }
        case AddEQBand: {
            auto* ab = static_cast<AddEQBandAction*>(pa);
            j["managerPath"] = ab->managerPath;
            j["nodeID"] = ab->nodeID;
            j["bandIndex"] = ab->bandIndex;
            j["bandState"] = ab->bandState;
            break;
        }
        case RemoveEQBand: {
            auto* rb = static_cast<RemoveEQBandAction*>(pa);
            j["managerPath"] = rb->managerPath;
            j["nodeID"] = rb->nodeID;
            j["bandIndex"] = rb->bandIndex;
            j["bandState"] = rb->bandState;
            break;
        }
        case VstParameterChange: {
            auto* vp = static_cast<VstParameterChangeAction*>(pa);
            j["managerPath"] = vp->managerPath;
            j["nodeID"] = vp->nodeID;
            j["paramID"] = vp->paramID;
            j["oldValue"] = vp->oldValue;
            j["newValue"] = vp->newValue;
            break;
        }
        case VstLoadPlugin: {
            auto* vl = static_cast<VstLoadPluginAction*>(pa);
            j["managerPath"] = vl->managerPath;
            j["nodeID"] = vl->nodeID;
            j["oldState"] = vl->oldState;
            j["newState"] = vl->newState;
            break;
        }
        case TogglePianoRollWindow: {
            auto* tw = static_cast<TogglePianoRollWindowAction*>(pa);
            j["managerPath"] = tw->managerPath;
            j["arrangerNodeID"] = tw->arrangerNodeID;
            j["regionID"] = tw->regionID;
            j["ewID"] = tw->ewID;
            j["x"] = tw->x;
            j["y"] = tw->y;
            j["w"] = tw->w;
            j["h"] = tw->h;
            j["zOrder"] = tw->zOrder;
            j["open"] = tw->open;
            break;
        }
        case SongRollRhythmEdo: {
            auto* sr = static_cast<SongRollRhythmEdoAction*>(pa);
            j["managerPath"] = sr->managerPath;
            j["nodeID"] = sr->nodeID;
            j["before"] = sr->before;
            j["after"] = sr->after;
            break;
        }
        case CreateAutomationCurve: {
            auto* ca = static_cast<CreateAutomationCurveAction*>(pa);
            j["managerPath"] = ca->managerPath;
            j["nodeID"] = ca->nodeID;
            j["curveID"] = ca->curveID;
            j["curveSnapshot"] = ca->curveSnapshot;
            j["snapshotValid"] = ca->snapshotValid;
            break;
        }
        case CreateAudioClip: {
            auto* ca = static_cast<CreateAudioClipAction*>(pa);
            j["managerPath"] = ca->managerPath;
            j["nodeID"] = ca->nodeID;
            j["clipID"] = ca->clipID;
            j["clipSnapshot"] = ca->clipSnapshot;
            j["snapshotValid"] = ca->snapshotValid;
            break;
        }
        case ModifyCurvePoints: {
            auto* mc = static_cast<ModifyCurvePointsAction*>(pa);
            j["managerPath"] = mc->managerPath;
            j["nodeID"] = mc->nodeID;
            j["curveID"] = mc->curveID;
            j["before"] = mc->before;
            j["after"] = mc->after;
            break;
        }
        default:
            throw std::runtime_error("invalid undo action type when serializing");
    }

    json children = json::array();
    for (auto c : pa->children) {
        json jc = ProjectAction::serialize(c);
        children.push_back(jc);
    }

    j["children"] = children;
    j["name"] = pa->name;
    j["last_index"] = pa->last_index;
    j["undoTreeExpanded"] = pa->undoTreeExpanded;
    if (!pa->savedMainManager.is_null())
        j["savedMainManager"] = pa->savedMainManager;
    return j;
}

