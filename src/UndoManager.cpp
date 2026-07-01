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

/** Each thread sets this once to the copy it owns (GUI → guiManager, audio → audioManager). */
static thread_local NodeManager* tls_activeManager = nullptr;

void NodeProcessor::setThreadActiveRoot(NodeManager* r) { tls_activeManager = r; }

static NodeManager& requireManager(Project* p, const std::vector<int>& path) {
    if (!p || !p->processor)
        throw std::runtime_error("requireManager: project or processor missing");
    NodeManager* nm = tls_activeManager;
    for (size_t i = 0; i < path.size(); ++i) {
        int nodeId = path[i];
        auto* node = nm->getNode(static_cast<uint16_t>(nodeId));
        auto* patcher = dynamic_cast<PatcherNode*>(node);
        if (patcher && patcher->mainManager) {
            nm = patcher->mainManager;
            continue;
        }
        auto* mux = dynamic_cast<MultiplexerNode*>(node);
        if (mux) {
            ++i;
            if (i >= path.size())
                throw std::runtime_error("requireManager: multiplexer without patcher index");
            int patcherId = path[i];
            for (auto* mp : mux->patchers) {
                if (mp->id == static_cast<uint16_t>(patcherId)) {
                    nm = mp->mainManager;
                    break;
                }
            }
            continue;
        }
        throw std::runtime_error("requireManager: invalid node in managerPath");
    }
    return *nm;
}

void UndoManager::newAction(ProjectAction* pa) {
    // --- Multiplexer replication: collect all mux levels in the path,
    //     then generate the full cartesian product of sibling paths. ---
    json j = ProjectAction::serialize(pa);
    if (j.contains("managerPath")) {
        auto path = j["managerPath"].get<std::vector<int>>();

        struct MuxLevel {
            size_t pathPos;
            std::vector<int> siblingIds;
        };
        std::vector<MuxLevel> levels;

        NodeManager* nm = tls_activeManager;
        for (size_t pi = 0; pi < path.size() && nm; ++pi) {
            int nodeId = path[pi];
            auto* node = nm->getNode(static_cast<uint16_t>(nodeId));
            auto* patcher = dynamic_cast<PatcherNode*>(node);
            auto* mux2 = dynamic_cast<MultiplexerNode*>(node);
            if (mux2) {
                ++pi;
                if (pi >= path.size()) break;
                int innerPatcherId = path[pi];
                patcher = nullptr;
                for (auto* mp : mux2->patchers) {
                    if (mp->id == static_cast<uint16_t>(innerPatcherId)) {
                        patcher = mp; break;
                    }
                }
            }
            if (patcher && patcher->multiplexer) {
                MuxLevel level;
                level.pathPos = pi;
                level.siblingIds.push_back(patcher->id); // index 0 = keep original
                for (auto* sib : patcher->multiplexer->patchers)
                    if (sib != patcher) level.siblingIds.push_back(sib->id);
                levels.push_back(level);
            }
            if (patcher && patcher->mainManager)
                nm = patcher->mainManager;
            else if (!mux2)
                break;
        }

        if (!levels.empty()) {
            // Build the full cartesian product of replacement combinations.
            std::vector<std::vector<int>> siblingPaths;
            {
                std::vector<size_t> indices(levels.size(), 0);
                std::vector<size_t> maxIdx;
                for (auto& lv : levels) maxIdx.push_back(lv.siblingIds.size());
                while (true) {
                    auto sp = path;
                    for (size_t li = 0; li < levels.size(); ++li)
                        sp[levels[li].pathPos] = levels[li].siblingIds[indices[li]];
                    siblingPaths.push_back(sp);

                    // Advance indices (mixed-radix counter, skip all-zeros = source).
                    size_t li = 0;
                    while (li < levels.size()) {
                        ++indices[li];
                        if (indices[li] < maxIdx[li]) break;
                        indices[li] = 0;
                        ++li;
                    }
                    if (li >= levels.size()) break;
                }
                // Remove the all-zeros entry (it's the source path, pushed first).
                if (!siblingPaths.empty()) siblingPaths.erase(siblingPaths.begin());
            }

            Project* proj = pa->p;
            auto origDo = pa->doAction;
            auto origUndo = pa->undoAction;
            std::shared_ptr<json> postDoJson = std::make_shared<json>();

            pa->doAction = [origDo, proj, siblingPaths, postDoJson, pa]() {
                origDo();
                *postDoJson = ProjectAction::serialize(pa);
                for (auto& sp : siblingPaths) {
                    json sj = *postDoJson;
                    sj["managerPath"] = sp;
                    auto* s = ProjectAction::deSerialize(sj, proj);
                    s->doAction();
                    delete s;
                }
            };
            pa->undoAction = [origUndo, proj, siblingPaths, postDoJson]() {
                for (int i = (int)siblingPaths.size() - 1; i >= 0; --i) {
                    json sj = *postDoJson;
                    sj["managerPath"] = siblingPaths[i];
                    auto* s = ProjectAction::deSerialize(sj, proj);
                    s->undoAction();
                    delete s;
                }
                origUndo();
            };

            if (pa->type == ZoomNodes) {
                // Zoom removed — no-op propagation for backward compat.
                auto* zn = static_cast<ZoomNodesAction*>(pa);
                zn->propagateCoalesced = [](float, float, float) {};
            }

            if (pa->skipInitialDo) {
                for (auto& sp : siblingPaths) {
                    std::cout << "[mux-replicate] type=" << static_cast<int>(pa->type)
                              << " nodeID=" << j.value("nodeID", -1) << " siblingPath=[";
                    for (size_t pi = 0; pi < sp.size(); ++pi)
                        std::cout << (pi ? "," : "") << sp[pi];
                    std::cout << "]" << std::endl;
                    json sj = j;
                    sj["managerPath"] = sp;
                    auto* s = ProjectAction::deSerialize(sj, proj);
                    s->doAction();
                    delete s;
                }
            }
        }
    }

    // --- Normal newAction flow ---
    current->newAction(pa);
    current->last_index = pa->index;
    current = pa;
    // Apply to GUI copy (active root defaults to GUI), unless already done via direct mutation.
    if (!pa->skipInitialDo)
        pa->doAction();
    // Always enqueue for audio copy replay before next DSP pass.
    if (pa->p && pa->p->processor && pa->p->processor->audioManager) {
        ProjectAction* cap = pa;
        enqueueAudioSync([cap]() { cap->doAction(); });
    }
}

void UndoManager::undo() {
    if (current == head) return;
    // Apply undo to GUI copy (active root is already GUI).
    current->undoAction();
    // Enqueue for audio copy (applied before next DSP callback).
    ProjectAction* cap = current;
    enqueueAudioSync([cap]() { cap->undoAction(); });
    current->parent->last_index = current->index;
    current = current->parent;
}

Region* undoResolveArrangerRegion(Project* p, const std::vector<int>& managerPath, int nodeID, int regionID) {
    NodeManager& nm = requireManager(p, managerPath);
    auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
    ElementManager* em = arr ? arr->elements : nullptr;
    if (!arr || !em)
        throw std::runtime_error("undoResolveArrangerRegion: arranger or element manager missing");
    auto* r = dynamic_cast<Region*>(em->getElement(static_cast<uint16_t>(regionID)));
    if (!r)
        throw std::runtime_error("undoResolveArrangerRegion: region not found");
    return r;
}

ArrangerNode* undoResolveArrangerNode(Project* p, const std::vector<int>& managerPath, int nodeID) {
    NodeManager& nm = requireManager(p, managerPath);
    auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
    if (!arr)
        throw std::runtime_error("undoResolveArrangerNode: arranger not found");
    return arr;
}

ElementManager* undoResolveArrangerElementManager(Project* p, const std::vector<int>& managerPath, int nodeID) {
    NodeManager& nm = requireManager(p, managerPath);
    auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
    return arr ? arr->elements : nullptr;
}

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
        {"toggle_piano_roll_window", TogglePianoRollWindow},
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
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int,"start":fract_json,"length":fract_json,"pitch":float,"pitchIntegerPairs":[[int,int],...]})";
    }
    if (actionName == "create_region") {
        return R"({"managerPath":[int,...],"nodeID":int})";
    }
    if (actionName == "delete_region") {
        return R"({"managerPath":[int,...],"nodeID":int,"regionID":int})";
    }
    if (actionName == "create_position") {
        return R"({"managerPath":[int,...],"nodeID":int,"elementID":int,"start":fract_json,"trackID":int})";
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
            for (const auto& el : params.at("pitchIntegerPairs")) {
                pairs.push_back({el.at(0).get<int>(), el.at(1).get<int>()});
            }
            pa = new CreateNoteAction(head->p, managerPath, params.at("nodeID").get<int>(), params.at("regionID").get<int>(),
                fract::fromJSON(params.at("start")), fract::fromJSON(params.at("length")), params.at("pitch").get<float>(),
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
        case CreatePosition:
            pa = new CreatePositionAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(),
                params.at("elementID").get<int>(), fract::fromJSON(params.at("start")),
                static_cast<uint16_t>(params.at("trackID").get<int>()));
            break;
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

ProjectAction* ProjectAction::deSerialize(json j, Project* p) {
    ProjectAction* pa;
    switch (j.at("type").get<int>()) {
        case CreateNote: {
            auto managerPath = j.at("managerPath").get<std::vector<int>>();
            std::vector<std::pair<int, int>> pairs;
            for (const auto& el : j.at("pitchIntegerPairs")) {
                pairs.push_back({el.at(0).get<int>(), el.at(1).get<int>()});
            }
            auto cn = new CreateNoteAction(p, managerPath, j.at("nodeID").get<int>(), j.at("regionID").get<int>(), fract::fromJSON(j.at("start")),
                fract::fromJSON(j.at("length")), j.at("pitch").get<float>(), std::move(pairs));
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
            auto cpp = new CreatePositionAction(p, managerPath, j.at("nodeID").get<int>(), j.at("elementID").get<int>(),
                fract::fromJSON(j.at("start")), static_cast<uint16_t>(j.at("trackID").get<int>()));
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
            j["start"] = cn->start.toJSON();
            j["length"] = cn->length.toJSON();
            j["pitch"] = cn->pitch;
            j["pitchIntegerPairs"] = json::array();
            for (const auto& pr : cn->pitchIntegerPairs)
                j["pitchIntegerPairs"].push_back(json::array({pr.first, pr.second}));
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
            j["start"] = cp->start.toJSON();
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
    return j;
}

void UndoManager::redo(int childIndex) {
    if (current->children.empty())
        return;
    int idx = childIndex;
    if (idx < 0) {
        idx = current->last_index;
        if (idx < 0 || static_cast<size_t>(idx) >= current->children.size())
            idx = static_cast<int>(current->children.size()) - 1;
        goTo(current->children[static_cast<size_t>(idx)]);
        return;
    }
    if (idx < 0 || static_cast<size_t>(idx) >= current->children.size())
        idx = static_cast<int>(current->children.size()) - 1;
    current = current->children[static_cast<size_t>(idx)];
    // Apply to GUI copy.
    current->doAction();
    // Enqueue for audio copy.
    ProjectAction* cap = current;
    enqueueAudioSync([cap]() { cap->doAction(); });
}

bool UndoManager::mouseHitsRect(SDL_FRect* rect) const {
    if (!hitTestWindow)
        return MouseOn(rect);
    float gx = 0.0f;
    float gy = 0.0f;
    SDL_GetGlobalMouseState(&gx, &gy);
    int wx = 0;
    int wy = 0;
    SDL_GetWindowPosition(hitTestWindow, &wx, &wy);
    const float lx = gx - static_cast<float>(wx);
    const float ly = gy - static_cast<float>(wy);
    return lx >= rect->x && lx < rect->x + rect->w && ly >= rect->y && ly < rect->y + rect->h;
}

void UndoManager::clearAllRenderTextures() {
    const std::function<void(ProjectAction*)> wipe = [&](ProjectAction* pa) {
        if (pa->texture) {
            SDL_DestroyTexture(pa->texture);
            pa->texture = nullptr;
        }
        for (ProjectAction* c : pa->children)
            wipe(c);
    };
    if (head)
        wipe(head);
}

namespace {

ProjectAction* directChildOnPath(ProjectAction* root, ProjectAction* descendant) {
    if (!root || !descendant || descendant == root)
        return nullptr;
    for (ProjectAction* n = descendant; n; n = n->parent) {
        if (n->parent == root)
            return n;
        if (n == root)
            return nullptr;
    }
    return nullptr;
}

struct UndoTreeProbe {
    ProjectAction* hit = nullptr;
    float bottomY = 0.f;
};

UndoTreeProbe probeUndoTreeAt(float lx, float ly, float x, float y, float layoutFullWidth, float rowH, ProjectAction* pa) {
    SDL_FRect row{x, y, layoutFullWidth, rowH};
    ProjectAction* selfHit = nullptr;
    if (lx >= row.x && lx < row.x + row.w && ly >= row.y && ly < row.y + row.h)
        selfHit = pa;
    float cursorY = y + rowH;
    if (!pa->undoTreeExpanded || pa->children.empty())
        return {selfHit, cursorY};
    const float childLeft = x + rowH;
    ProjectAction* best = selfHit;
    for (ProjectAction* c : pa->children) {
        UndoTreeProbe sub = probeUndoTreeAt(lx, ly, childLeft, cursorY, layoutFullWidth, rowH, c);
        if (sub.hit)
            best = sub.hit;
        cursorY = sub.bottomY;
    }
    return {best, cursorY};
}

bool undoNodeChainReachHead(ProjectAction* n, ProjectAction* realHead) {
    while (n) {
        if (n == realHead)
            return true;
        n = n->parent;
    }
    return false;
}

void sanitizeUndoTreeViewRoot(UndoManager& um) {
    if (!um.head) {
        um.undoTreeViewRoot = nullptr;
        return;
    }
    if (um.undoTreeViewRoot && !undoNodeChainReachHead(um.undoTreeViewRoot, um.head))
        um.undoTreeViewRoot = nullptr;
}

} // namespace

void UndoManager::undoTreeHandleWheel(const SDL_FRect& layoutAnchor, float rowPixels, float mouseX, float mouseY,
    float wheelY) {
    if (!head)
        return;
    sanitizeUndoTreeViewRoot(*this);
    const float rowH = rowPixels > 0.f ? rowPixels : undoTreeRowH;
    ProjectAction* vr = undoTreeViewRoot ? undoTreeViewRoot : head;

    constexpr float eps = 0.05f;
    if (std::fabs(wheelY) < eps)
        return;

    /* SDL: positive Y scrolls finger away — treat as drill down into a child row. */
    if (wheelY < 0.f) {
        if (vr != head && vr->parent)
            undoTreeViewRoot = (vr->parent == head) ? nullptr : vr->parent;
        return;
    }

    ProjectAction* h = probeUndoTreeAt(mouseX, mouseY, layoutAnchor.x, layoutAnchor.y, layoutAnchor.w, rowH, vr).hit;

    ProjectAction* const cChild = (current != vr) ? directChildOnPath(vr, current) : nullptr;
    ProjectAction* const hChild = (h && h != vr) ? directChildOnPath(vr, h) : nullptr;

    /* Prefer the branch under the hovered row; otherwise follow the path to the undo tip. */
    ProjectAction* next = hChild ? hChild : cChild;

    if (!next && !vr->children.empty()) {
        int idx = vr->last_index;
        const int nch = static_cast<int>(vr->children.size());
        if (idx < 0 || idx >= nch)
            idx = nch - 1;
        next = vr->children[static_cast<size_t>(idx)];
    }

    if (next)
        undoTreeViewRoot = next;
}

void UndoManager::syncUndoTreeExpansionPathToCurrent() {
    if (!head || !current)
        return;
    for (ProjectAction* n = current; n && n != head; n = n->parent) {
        if (n->parent)
            n->parent->undoTreeExpanded = true;
    }
}

void UndoManager::applyUndoTreeClickAfterLayout() {
    const bool left = undoTreeFrameLeft;
    const bool right = undoTreeFrameRight;
    undoTreeFrameLeft = false;
    undoTreeFrameRight = false;
    ProjectAction* hit = undoTreeHitUnderCursor;
    undoTreeHitUnderCursor = nullptr;
    if (!hit)
        return;
    /* Prefer navigation if both landed same frame (e.g. odd hardware). */
    if (left) {
        goTo(hit);
        return;
    }
    if (right && !hit->children.empty())
        hit->undoTreeExpanded = !hit->undoTreeExpanded;
}

bool UndoManager::drawUndoTreeRow(SDL_Renderer* renderer, SDL_FRect* rect, ProjectAction* pa) {
    bool hovering = false;

    SDL_Color color;
    if (pa == current) {
        if (mouseHitsRect(rect))
            color = {100, 200, 100, 255};
        else
            color = {100, 255, 100, 255};
    } else {
        if (mouseHitsRect(rect))
            color = {255, 255, 255, 255};
        else
            color = {200, 200, 200, 255};
    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_Texture*& texture = pa->texture;
    if (!texture) {
        SDL_Color textColor{0, 0, 0, 255};
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, pa->name.c_str(), 0, textColor);
        texture = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);
    }

    SDL_RenderTexture(renderer, texture, nullptr, rect);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderRect(renderer, rect);

    if (mouseHitsRect(rect)) {
        hovering = true;
        if (undoTreeFrameLeft || undoTreeFrameRight)
            undoTreeHitUnderCursor = pa;
    }

    return hovering;
}

UndoManager::UndoTreeLayoutBox UndoManager::layoutUndoTreeGeom(SDL_Renderer* renderer, float x, float y, float w, float rowH, ProjectAction* pa) {
    UndoTreeLayoutBox out;
    SDL_FRect row{x, y, w, rowH};
    out.hovering = drawUndoTreeRow(renderer, &row, pa);
    float cursorY = y + rowH;

    if (!pa->undoTreeExpanded || pa->children.empty()) {
        out.bottomY = cursorY;
        return out;
    }

    const float kIndent = rowH;
    const float childLeft = x + kIndent;
    const float childW = baseRect->w;
    const float trunkX = row.x + kIndent * 0.5f;

    std::vector<float> directChildRowMidY;
    directChildRowMidY.reserve(pa->children.size());
    for (ProjectAction* c : pa->children) {
        const float sliceTop = cursorY;
        directChildRowMidY.push_back(sliceTop + rowH * 0.5f);

        UndoTreeLayoutBox sub =
            layoutUndoTreeGeom(renderer, childLeft, cursorY, childW, rowH, c);
        out.hovering |= sub.hovering;
        cursorY = sub.bottomY;
    }

    /* Vertical spine to the last sibling’s row mid; one horizontal per direct child at its own row.
       Expanding a child only adds pixels below that row — earlier horizontals stay put. */
    const float trunkTop = row.y + row.h;
    const float spineBottom = directChildRowMidY.back();
    const float hRunEnd = std::fmax(childLeft - 2.5f, trunkX);

    SDL_SetRenderDrawColor(renderer, 160, 160, 164, 255);
    SDL_RenderLine(renderer, trunkX, trunkTop + 0.5f, trunkX, spineBottom + 0.5f);
    for (float midY : directChildRowMidY)
        SDL_RenderLine(renderer, trunkX, midY + 0.5f, hRunEnd, midY + 0.5f);

    out.bottomY = cursorY;
    return out;
}

bool UndoManager::render(SDL_Renderer* renderer) {
    if (!baseRect || !head)
        return false;
    sanitizeUndoTreeViewRoot(*this);
    syncUndoTreeExpansionPathToCurrent();
    undoTreeHitUnderCursor = nullptr;
    undoTreeFrameLeft = undoTreePendingLeft;
    undoTreeFrameRight = undoTreePendingRight;
    undoTreePendingLeft = false;
    undoTreePendingRight = false;
    const float rowH = undoTreeRowH > 1.f ? undoTreeRowH : 20.f;
    auto viewRoot = [&]() -> ProjectAction* { return undoTreeViewRoot ? undoTreeViewRoot : head; };
    UndoTreeLayoutBox box =
        layoutUndoTreeGeom(renderer, baseRect->x, baseRect->y, baseRect->w, rowH, viewRoot());
    ProjectAction* const tipBeforeClick = current;
    applyUndoTreeClickAfterLayout();
    syncUndoTreeExpansionPathToCurrent();
    sanitizeUndoTreeViewRoot(*this);
    if (current != tipBeforeClick)
        box = layoutUndoTreeGeom(renderer, baseRect->x, baseRect->y, baseRect->w, rowH, viewRoot());
    return box.hovering;
}

void UndoManager::goTo(ProjectAction* target) {
    // traverse the tree to some arbitrary action node

    if (!target || !current || !head)
        throw std::runtime_error("UndoManager::goTo: null target, current, or head");

    std::vector<int> headToCurrent;
    std::vector<int> headToTarget;

    auto tmp = current;
    while (tmp != head) {
        headToCurrent.push_back(tmp->index);
        tmp = tmp->parent;
    }
    std::reverse(headToCurrent.begin(), headToCurrent.end());

    tmp = target;
    while (tmp != head) {
        headToTarget.push_back(tmp->index);
        tmp = tmp->parent;
    }
    std::reverse(headToTarget.begin(), headToTarget.end());

    // find divergence point
    size_t d = 0;
    while (d < headToCurrent.size() && d < headToTarget.size() && headToCurrent[d] == headToTarget[d]) d++;

    // travel to divergence point

    size_t i = headToCurrent.size();
    while (i > d) {
        undo();
        i--;
    }

    // travel to target

    while (i < headToTarget.size()) {
        redo(headToTarget[i]);
        i++;
    }
}


CreateNoteAction::CreateNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, fract start,
                                   fract length, float pitch, std::vector<std::pair<int, int>> pitchIntegerPairs) :
        ProjectAction(p, CreateNote),
        managerPath(std::move(managerPath)),
        regionID(regionID),
        start(start),
        length(length),
        pitch(pitch),
        pitchIntegerPairs(std::move(pitchIntegerPairs)),
        nodeID(nodeID) {
    doAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        noteID = region.createNote(this->start, this->length, this->pitch, this->pitchIntegerPairs);
        name = "Create Note " + std::to_string(noteID) + " " + std::to_string(this->regionID);
        if (!noteStampedSnapshot.is_null()) {
            auto it = region.id_to_index.find(noteID);
            if (it != region.id_to_index.end()) {
                const size_t idx = static_cast<size_t>(it->second);
                if (idx < region.notes.size() && region.notes[idx])
                    region.notes[idx]->applyUndoSnapshot(noteStampedSnapshot);
            }
        }
    };

    undoAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        region.deleteNote(this->noteID);
    };
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
            arr->sl->clearPianoRoll(t->regionID);
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

void DeleteRegionAction::wireDeleteRegionLambdas() {
    doAction = [this]() {
        ElementManager* em = undoResolveArrangerElementManager(this->p, this->managerPath, this->nodeID);
        if (!em)
            throw std::runtime_error("DeleteRegionAction::doAction: element manager missing");
        ArrangerNode* arr = undoResolveArrangerNode(this->p, this->managerPath, this->nodeID);
        if (arr && arr->sl)
            arr->sl->clearPianoRoll(this->regionID);
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

CreatePositionAction::CreatePositionAction(Project* p, std::vector<int> managerPath, int nodeID, int elementID, fract start, uint16_t trackID) :
        ProjectAction(p, CreatePosition),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        elementID(elementID),
        start(std::move(start)),
        trackID(trackID) {
    doAction = [this]() {
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        ge->createPos(this->start, this->trackID);
        this->positionID = ge->positions.back()->id;
        this->name = "Create Position " + std::to_string(this->positionID);
    };
    undoAction = [this]() {
        GridElement* ge = undoResolveGridElement(this->p, this->managerPath, this->nodeID, this->elementID);
        if (!ge->removePositionById(this->positionID))
            throw std::runtime_error("CreatePositionAction::undoAction: position id missing");
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

MoveEmbeddedWindowAction::MoveEmbeddedWindowAction(Project* p, std::vector<int> managerPath, int ewID, float fromX, float fromY, float toX, float toY) :
        ProjectAction(p, MoveEmbeddedWindow),
        managerPath(std::move(managerPath)),
        ewID(ewID),
        fromX(fromX), fromY(fromY),
        toX(toX), toY(toY) {
    name = "Move Embedded Window";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (!nm.ne) return; // audio thread: GUI-only action
        EmbeddedWindow* ew = nm.ne->getEmbeddedWindowById(this->ewID);
        ew->moveTo(this->toX, this->toY);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (!nm.ne) return;
        EmbeddedWindow* ew = nm.ne->getEmbeddedWindowById(this->ewID);
        ew->moveTo(this->fromX, this->fromY);
    };
}

ResizeEmbeddedWindowAction::ResizeEmbeddedWindowAction(Project* p, std::vector<int> managerPath, int ewID,
        float fromX, float fromY, float fromW, float fromH,
        float toX, float toY, float toW, float toH) :
        ProjectAction(p, ResizeEmbeddedWindow),
        managerPath(std::move(managerPath)),
        ewID(ewID),
        fromX(fromX), fromY(fromY), fromW(fromW), fromH(fromH),
        toX(toX), toY(toY), toW(toW), toH(toH) {
    name = "Resize Embedded Window";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (!nm.ne) return; // audio thread: GUI-only action
        EmbeddedWindow* ew = nm.ne->getEmbeddedWindowById(this->ewID);
        ew->applyGeometry(this->toX, this->toY, this->toW, this->toH);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (!nm.ne) return;
        EmbeddedWindow* ew = nm.ne->getEmbeddedWindowById(this->ewID);
        ew->applyGeometry(this->fromX, this->fromY, this->fromW, this->fromH);
    };
}

ToggleNodeVisibleAction::ToggleNodeVisibleAction(Project* p, std::vector<int> managerPath, int nodeId) :
        ProjectAction(p, ToggleNodeVisible),
        managerPath(std::move(managerPath)),
        nodeId(nodeId) {
    name = "Toggle Node Visible";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* n = nm.getNode(static_cast<uint16_t>(this->nodeId));
        n->visible = !n->visible;
    };
    undoAction = doAction;
}

PanNodesAction::PanNodesAction(Project* p, std::vector<int> managerPath, float dx, float dy) :
        ProjectAction(p, PanNodes),
        managerPath(std::move(managerPath)),
        dx(dx), dy(dy) {
    name = "Pan View";
    skipInitialDo = true;
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        for (auto n : nm.getNodes()) n->move(n->dstRect.x + this->dx, n->dstRect.y + this->dy);
        nm.inNode->move(nm.inNode->dstRect.x + this->dx, nm.inNode->dstRect.y + this->dy);
        nm.outNode->move(nm.outNode->dstRect.x + this->dx, nm.outNode->dstRect.y + this->dy);
        if (nm.ne) { nm.ne->panOffsetX_ += this->dx; nm.ne->panOffsetY_ += this->dy; }
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        for (auto n : nm.getNodes()) n->move(n->dstRect.x - this->dx, n->dstRect.y - this->dy);
        nm.inNode->move(nm.inNode->dstRect.x - this->dx, nm.inNode->dstRect.y - this->dy);
        nm.outNode->move(nm.outNode->dstRect.x - this->dx, nm.outNode->dstRect.y - this->dy);
        if (nm.ne) { nm.ne->panOffsetX_ -= this->dx; nm.ne->panOffsetY_ -= this->dy; }
    };
}

ZoomNodesAction::ZoomNodesAction(Project* p, std::vector<int> managerPath, float amount, float mx, float my) :
        ProjectAction(p, ZoomNodes),
        managerPath(std::move(managerPath)) {
    name = "Zoom View";
    skipInitialDo = true;
    amounts.push_back(amount);
    mxs.push_back(mx);
    mys.push_back(my);
    // Zoom removed — actions are no-ops for backward compat with serialized history.
    doAction = [](){};
    undoAction = [](){};
}

void ZoomNodesAction::addStep(float amount, float mx, float my) {
    amounts.push_back(amount);
    mxs.push_back(mx);
    mys.push_back(my);
    if (propagateCoalesced)
        propagateCoalesced(amount, mx, my);
}

AddNodeAction::AddNodeAction(Project* p, std::vector<int> managerPath, int type, float x, float y) :
        ProjectAction(p, AddNode),
        managerPath(std::move(managerPath)),
        nodeType(type),
        x(x),
        y(y) {
    name ="Add Node";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (hasRedoRestore) {
            if (!patcherData.is_null()) {
                for (auto& pd : patcherData) {
                    auto* p = nm.addNodeNow(pd["node"]);
                    if (p) {
                        for (auto c : pd["conns"])
                            nm.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
                    }
                }
            }
            json snap = redoNodeSnapshot;
            if (nm.ne) {
                snap["x"] = snap["x"].get<float>() + nm.ne->panOffsetX_ - this->panOffX;
                snap["y"] = snap["y"].get<float>() + nm.ne->panOffsetY_ - this->panOffY;
            }
            auto* restored = nm.addNodeNow(snap);
            if (!restored)
                throw std::runtime_error("AddNodeAction::doAction: addNodeNow(json) failed");
            nodeID = restored->id;
            for (auto c : redoConnectionsSnapshot)
                nm.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
            return;
        }
        float ax = this->x, ay = this->y;
        if (nm.ne) {
            ax += nm.ne->panOffsetX_ - this->panOffX;
            ay += nm.ne->panOffsetY_ - this->panOffY;
        }
        auto* node = nm.addNodeNow(static_cast<NodeType>(nodeType), ax, ay, nodeID);
        if (!node)
            throw std::runtime_error("AddNodeAction::doAction: addNodeNow failed");
        nodeID = node->id;
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (nodeID < 0)
            throw std::runtime_error("AddNodeAction::undoAction: invalid nodeID");
        if (!nm.snapshotNode(static_cast<uint16_t>(nodeID), redoNodeSnapshot, redoConnectionsSnapshot))
            throw std::runtime_error("AddNodeAction::undoAction: snapshotNode failed");
        hasRedoRestore = true;
        auto* mux = dynamic_cast<MultiplexerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
        if (mux) {
            patcherData = json::array();
            std::vector<uint16_t> pids;
            for (auto* p : mux->patchers) {
                json pd;
                (void)nm.snapshotNode(p->id, pd["node"], pd["conns"]);
                patcherData.push_back(pd);
                pids.push_back(p->id);
            }
            mux->patchers.clear();
            for (auto pid : pids)
                nm.removeNodeNow(pid);
        }
        nm.removeNodeNow(nodeID);
    };
}

RemoveNodeAction::RemoveNodeAction(Project* p, std::vector<int> managerPath, int nodeID) :
        ProjectAction(p, RemoveNode),
        managerPath(std::move(managerPath)),
        nodeID(nodeID) {
    name ="Remove Node";
    doAction = [this] () {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        if (nodeData.is_null()) {
            (void)nm2.snapshotNode(static_cast<uint16_t>(this->nodeID), nodeData, connectionsData);
            auto* mux = dynamic_cast<MultiplexerNode*>(nm2.getNode(static_cast<uint16_t>(this->nodeID)));
            if (mux) {
                patcherData = json::array();
                std::vector<uint16_t> pids;
                for (auto* p : mux->patchers) {
                    json pd;
                    (void)nm2.snapshotNode(p->id, pd["node"], pd["conns"]);
                    patcherData.push_back(pd);
                    pids.push_back(p->id);
                }
                mux->patchers.clear();
                for (auto pid : pids)
                    nm2.removeNodeNow(pid);
            }
        }
        nm2.removeNodeNow(this->nodeID);
    };
    undoAction = [this] () {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        if (!patcherData.is_null()) {
            for (auto& pd : patcherData) {
                auto* p = nm2.addNodeNow(pd["node"]);
                if (p) {
                    for (auto c : pd["conns"])
                        nm2.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
                }
            }
        }
        json nd = nodeData;
        if (nm2.ne) {
            nd["x"] = nd["x"].get<float>() + nm2.ne->panOffsetX_ - this->panOffX;
            nd["y"] = nd["y"].get<float>() + nm2.ne->panOffsetY_ - this->panOffY;
        }
        auto* restored = nm2.addNodeNow(nd);
        if (!restored)
            throw std::runtime_error("RemoveNodeAction::undoAction: addNodeNow failed");
        for (auto c : connectionsData)
            nm2.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
    };
}

MakeNodeConnectionAction::MakeNodeConnectionAction(Project* p, std::vector<int> managerPath, int srcNodeID, int srcConID, int dstNodeID, int dstConID) :
        ProjectAction(p, MakeNodeConnection),
        managerPath(std::move(managerPath)),
        srcNodeID(srcNodeID),
        srcConID(srcConID),
        dstNodeID(dstNodeID),
        dstConID(dstConID) {
    name ="Connect Nodes";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.makeNodeConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.severConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
}

SeverNodeConnectionAction::SeverNodeConnectionAction(Project* p, std::vector<int> managerPath, int srcNodeID, int srcConID, int dstNodeID, int dstConID) :
        ProjectAction(p, SeverNodeConnection),
        managerPath(std::move(managerPath)),
        srcNodeID(srcNodeID),
        srcConID(srcConID),
        dstNodeID(dstNodeID),
        dstConID(dstConID) {
    name ="Sever Connection";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.severConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.makeNodeConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
}

ReassignNodeConnectionAction::ReassignNodeConnectionAction(Project* p, std::vector<int> managerPath,
        const ConnIDs* oldCxns, int oldCount,
        int nSrcN, int nSrcC, int nDstN, int nDstC) :
        ProjectAction(p, ReassignNodeConnection),
        managerPath(std::move(managerPath)),
        oldConnCount(oldCount),
        newSrcNodeID(nSrcN), newSrcConID(nSrcC),
        newDstNodeID(nDstN), newDstConID(nDstC) {
    name = "Reassign Connection";
    for (int i = 0; i < oldCount; ++i) oldConns[i] = oldCxns[i];

    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        for (int i = 0; i < this->oldConnCount; ++i) {
            if (this->oldConns[i].existed)
                nm.severConnectionNow(static_cast<uint16_t>(this->oldConns[i].srcNodeID),
                                      static_cast<uint16_t>(this->oldConns[i].srcConID),
                                      static_cast<uint16_t>(this->oldConns[i].dstNodeID),
                                      static_cast<uint16_t>(this->oldConns[i].dstConID));
        }
        nm.makeNodeConnectionNow(static_cast<uint16_t>(this->newSrcNodeID),
                                 static_cast<uint16_t>(this->newSrcConID),
                                 static_cast<uint16_t>(this->newDstNodeID),
                                 static_cast<uint16_t>(this->newDstConID));
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.severConnectionNow(static_cast<uint16_t>(this->newSrcNodeID),
                              static_cast<uint16_t>(this->newSrcConID),
                              static_cast<uint16_t>(this->newDstNodeID),
                              static_cast<uint16_t>(this->newDstConID));
        for (int i = this->oldConnCount - 1; i >= 0; --i) {
            if (this->oldConns[i].existed)
                nm.makeNodeConnectionNow(static_cast<uint16_t>(this->oldConns[i].srcNodeID),
                                         static_cast<uint16_t>(this->oldConns[i].srcConID),
                                         static_cast<uint16_t>(this->oldConns[i].dstNodeID),
                                         static_cast<uint16_t>(this->oldConns[i].dstConID));
        }
    };
}

IoPortChannelAction::IoPortChannelAction(Project* p, int opIn, std::vector<int> managerPathIn, uint16_t connectionIdIn, size_t connectionIndexIn) :
        ProjectAction(p, IoPortChannel),
        managerPath(std::move(managerPathIn)),
        op(opIn),
        connectionId(connectionIdIn),
        connectionIndex(connectionIndexIn) {
    switch (op) {
        case IoPortChannelOp::InputAddWaveform:
            name = "Add input bus waveform";
            break;
        case IoPortChannelOp::InputRemoveWaveform:
            name = "Remove input bus waveform";
            idAssigned = true;
            break;
        case IoPortChannelOp::InputAddEvent:
            name = "Add input bus event output";
            break;
        case IoPortChannelOp::InputRemoveEvent:
            name = "Remove input bus event output";
            idAssigned = true;
            break;
        case IoPortChannelOp::OutputAddWaveform:
            name = "Add output bus waveform";
            break;
        case IoPortChannelOp::OutputRemoveWaveform:
            name = "Remove output bus waveform";
            idAssigned = true;
            break;
        case IoPortChannelOp::OutputAddEvent:
            name = "Add output bus event input";
            break;
        case IoPortChannelOp::OutputRemoveEvent:
            name = "Remove output bus event input";
            idAssigned = true;
            break;
        default:
            name = "I/O port channel";
            break;
    }

    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        InputNode* in = nm.inNode;
        OutputNode* out = nm.outNode;
        switch (this->op) {
            case IoPortChannelOp::InputAddWaveform: {
                if (!this->idAssigned) {
                    const size_t pos = in->countWaveformOutputs();
                    in->addWaveformOutputChannel();
                    this->connectionId = in->outputs.connections[pos]->id;
                    this->connectionIndex = pos;
                    this->idAssigned = true;
                } else if (in->outputs.getConnection(this->connectionId) == nullptr) {
                    in->insertWaveformOutputChannelAt(this->connectionIndex, this->connectionId);
                }
                break;
            }
            case IoPortChannelOp::InputRemoveWaveform:
                in->removeWaveformOutputById(this->connectionId);
                break;
            case IoPortChannelOp::InputAddEvent: {
                if (!this->idAssigned) {
                    in->addEventOutputSocket();
                    this->connectionId = in->outputs.connections.back()->id;
                    this->connectionIndex = in->outputs.connections.size() - 1;
                    this->idAssigned = true;
                } else if (in->outputs.getConnection(this->connectionId) == nullptr) {
                    in->insertEventOutputChannelAt(this->connectionIndex, this->connectionId);
                }
                break;
            }
            case IoPortChannelOp::InputRemoveEvent:
                in->removeEventOutputById(this->connectionId);
                break;
            case IoPortChannelOp::OutputAddWaveform: {
                if (!this->idAssigned) {
                    const size_t pos = out->countWaveformInputs();
                    out->addWaveformInputChannel();
                    this->connectionId = out->inputs.connections[pos]->id;
                    this->connectionIndex = pos;
                    this->idAssigned = true;
                } else if (out->inputs.getConnection(this->connectionId) == nullptr) {
                    out->insertWaveformInputChannelAt(this->connectionIndex, this->connectionId);
                }
                break;
            }
            case IoPortChannelOp::OutputRemoveWaveform:
                out->removeWaveformInputById(this->connectionId);
                break;
            case IoPortChannelOp::OutputAddEvent: {
                if (!this->idAssigned) {
                    out->addEventInputSocket();
                    this->connectionId = out->inputs.connections.back()->id;
                    this->connectionIndex = out->inputs.connections.size() - 1;
                    this->idAssigned = true;
                } else if (out->inputs.getConnection(this->connectionId) == nullptr) {
                    out->insertEventInputChannelAt(this->connectionIndex, this->connectionId);
                }
                break;
            }
            case IoPortChannelOp::OutputRemoveEvent:
                out->removeEventInputById(this->connectionId);
                break;
            default:
                break;
        }
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        InputNode* in = nm.inNode;
        OutputNode* out = nm.outNode;
        switch (this->op) {
            case IoPortChannelOp::InputAddWaveform:
                in->removeWaveformOutputById(this->connectionId);
                break;
            case IoPortChannelOp::InputRemoveWaveform:
                if (in->outputs.getConnection(this->connectionId) == nullptr)
                    in->insertWaveformOutputChannelAt(this->connectionIndex, this->connectionId);
                break;
            case IoPortChannelOp::InputAddEvent:
                in->removeEventOutputById(this->connectionId);
                break;
            case IoPortChannelOp::InputRemoveEvent:
                if (in->outputs.getConnection(this->connectionId) == nullptr)
                    in->insertEventOutputChannelAt(this->connectionIndex, this->connectionId);
                break;
            case IoPortChannelOp::OutputAddWaveform:
                out->removeWaveformInputById(this->connectionId);
                break;
            case IoPortChannelOp::OutputRemoveWaveform:
                if (out->inputs.getConnection(this->connectionId) == nullptr)
                    out->insertWaveformInputChannelAt(this->connectionIndex, this->connectionId);
                break;
            case IoPortChannelOp::OutputAddEvent:
                out->removeEventInputById(this->connectionId);
                break;
            case IoPortChannelOp::OutputRemoveEvent:
                if (out->inputs.getConnection(this->connectionId) == nullptr)
                    out->insertEventInputChannelAt(this->connectionIndex, this->connectionId);
                break;
            default:
                break;
        }
    };
}

MoveNoteAction::MoveNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID, json before,
                               json after, std::string actionName) :
        ProjectAction(p, MoveNote),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteID(noteID),
        before(std::move(before)),
        after(std::move(after)) {
    skipInitialDo = true;
    name = std::move(actionName);
    doAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        auto it = region.id_to_index.find(this->noteID);
        if (it == region.id_to_index.end())
            throw std::runtime_error("MoveNoteAction::doAction: note id not in region");
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= region.notes.size())
            throw std::runtime_error("MoveNoteAction::doAction: note index out of range");
        region.notes[idx]->applyUndoSnapshot(this->after);
    };
    undoAction = [this]() {
        Region& region = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        auto it = region.id_to_index.find(this->noteID);
        if (it == region.id_to_index.end())
            throw std::runtime_error("MoveNoteAction::undoAction: note id not in region");
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= region.notes.size())
            throw std::runtime_error("MoveNoteAction::undoAction: note index out of range");
        region.notes[idx]->applyUndoSnapshot(this->before);
    };
}

void DeleteNoteAction::wireDeleteLambdas() {
    doAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.deleteNote(this->noteID);
    };
    undoAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        json snap = this->noteSnapshot;
        std::shared_ptr<Note> n = Note::fromJSON(snap);
        r.restoreNoteAt(std::move(n), this->insertIndex);
    };
}

DeleteNoteAction::DeleteNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID) :
        ProjectAction(p, DeleteNote),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteID(noteID) {
    name = "Delete Note";
    Region& region = *undoResolveArrangerRegion(p, this->managerPath, nodeID, regionID);
    auto it = region.id_to_index.find(noteID);
    if (it == region.id_to_index.end())
        throw std::runtime_error("DeleteNoteAction: note id not in region");
    insertIndex = static_cast<size_t>(it->second);
    if (insertIndex >= region.notes.size() || !region.notes[insertIndex] || region.notes[insertIndex]->id != noteID)
        throw std::runtime_error("DeleteNoteAction: note index or id mismatch");
    noteSnapshot = region.notes[insertIndex]->toJSON();
    wireDeleteLambdas();
}

DeleteNoteAction::DeleteNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, int noteID,
                                   size_t insertIndex, json noteSnapshot) :
        ProjectAction(p, DeleteNote),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteID(noteID),
        insertIndex(insertIndex),
        noteSnapshot(std::move(noteSnapshot)) {
    name = "Delete Note";
    wireDeleteLambdas();
}

PianoRollRegionTuningUndoAction::PianoRollRegionTuningUndoAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID,
                                                                 json beforeRegion, json afterRegion, std::string actionName) :
        ProjectAction(p, PianoRollRegionTuning),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        beforeRegion(std::move(beforeRegion)),
        afterRegion(std::move(afterRegion)) {
    if (actionName.empty())
        throw std::runtime_error("PianoRollRegionTuningUndoAction: actionName must be non-empty");
    name = std::move(actionName);
    doAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.applyTuningUndoFromJSON(this->afterRegion);
        PianoRoll::notifyTuningUndoApplied(this->p, this->managerPath, this->nodeID, this->regionID, -1);
    };
    undoAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.applyTuningUndoFromJSON(this->beforeRegion);
        PianoRoll::notifyTuningUndoApplied(this->p, this->managerPath, this->nodeID, this->regionID, -1);
    };
}

AssignNoteHarmonicUndoAction::AssignNoteHarmonicUndoAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID,
                                                           int noteID, json beforeRegion, json afterRegion, json beforeNote,
                                                           json afterNote) :
        ProjectAction(p, AssignNoteHarmonic),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        regionID(regionID),
        noteID(noteID),
        beforeRegion(std::move(beforeRegion)),
        afterRegion(std::move(afterRegion)),
        beforeNote(std::move(beforeNote)),
        afterNote(std::move(afterNote)) {
    name = "Assign Note Harmonic";
    doAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.applyTuningUndoFromJSON(this->afterRegion);
        auto it = r.id_to_index.find(this->noteID);
        if (it == r.id_to_index.end())
            throw std::runtime_error("AssignNoteHarmonicUndoAction::doAction: note id not in region");
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= r.notes.size())
            throw std::runtime_error("AssignNoteHarmonicUndoAction::doAction: note index out of range");
        const std::shared_ptr<Note>& note = r.notes[idx];
        if (!note)
            throw std::runtime_error("AssignNoteHarmonicUndoAction::doAction: null note");
        note->applyTuningFieldsUndoFromJSON(this->afterNote);
        PianoRoll::notifyTuningUndoApplied(this->p, this->managerPath, this->nodeID, this->regionID, this->noteID);
    };
    undoAction = [this]() {
        Region& r = *undoResolveArrangerRegion(this->p, this->managerPath, this->nodeID, this->regionID);
        r.applyTuningUndoFromJSON(this->beforeRegion);
        auto it = r.id_to_index.find(this->noteID);
        if (it == r.id_to_index.end())
            throw std::runtime_error("AssignNoteHarmonicUndoAction::undoAction: note id not in region");
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= r.notes.size())
            throw std::runtime_error("AssignNoteHarmonicUndoAction::undoAction: note index out of range");
        const std::shared_ptr<Note>& note = r.notes[idx];
        if (!note)
            throw std::runtime_error("AssignNoteHarmonicUndoAction::undoAction: null note");
        note->applyTuningFieldsUndoFromJSON(this->beforeNote);
        PianoRoll::notifyTuningUndoApplied(this->p, this->managerPath, this->nodeID, this->regionID, this->noteID);
    };
}

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
        if (!vst || !vst->plugin) return;
        vst->plugin->setParameterValue(static_cast<int>(this->paramID), this->newValue);
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* vst = dynamic_cast<VstNode*>(node);
        if (!vst || !vst->plugin) return;
        vst->plugin->setParameterValue(static_cast<int>(this->paramID), this->oldValue);
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
            if (this->newState.contains("compState") && vst->plugin) {
                auto& arr = this->newState["compState"];
                std::vector<uint8_t> data(arr.begin(), arr.end());
                vst->plugin->setComponentState(data);
            }
            if (this->newState.contains("ctrlState") && vst->plugin) {
                auto& arr = this->newState["ctrlState"];
                std::vector<uint8_t> data(arr.begin(), arr.end());
                vst->plugin->setControllerState(data);
            }
        }
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* node = nm.getNode(static_cast<uint16_t>(this->nodeID));
        auto* vst = dynamic_cast<VstNode*>(node);
        if (!vst) return;
        std::string path = this->oldState.is_null() ? "" : this->oldState.value("path", "");
        vst->loadPlugin(path);
        if (!this->oldState.is_null()) {
            vst->bypass.value = this->oldState.value("bypass", 0.0f);
            if (this->oldState.contains("compState") && vst->plugin) {
                auto& arr = this->oldState["compState"];
                std::vector<uint8_t> data(arr.begin(), arr.end());
                vst->plugin->setComponentState(data);
            }
            if (this->oldState.contains("ctrlState") && vst->plugin) {
                auto& arr = this->oldState["ctrlState"];
                std::vector<uint8_t> data(arr.begin(), arr.end());
                vst->plugin->setControllerState(data);
            }
        }
    };
}

static void openPianoRollWindow(Project* p, const std::vector<int>& managerPath, int arrangerNodeID, int regionID,
                                int ewID, float x, float y, float w, float h, int zOrder) {
    NodeManager& nm = requireManager(p, managerPath);
    if (!nm.ne) return; // audio thread: GUI-only action
    ArrangerNode* arr = undoResolveArrangerNode(p, managerPath, arrangerNodeID);
    if (!arr) return;
    arr->ensureSongRoll();
    if (!arr->sl) return;
    ElementManager* em = arr->elements;
    if (!em) return;
    auto* region = dynamic_cast<Region*>(em->getElement(static_cast<uint16_t>(regionID)));
    if (!region) return;
    arr->sl->createPianoRoll(region, false, ewID);
    if (!arr->sl->pianoRollWindows.empty()) {
        auto* prw = arr->sl->pianoRollWindows.back();
        if (prw) {
            prw->setPosition(static_cast<int>(x), static_cast<int>(y));
            prw->setSize(static_cast<int>(w), static_cast<int>(h));
        }
    }
}

static void closePianoRollWindow(Project* p, const std::vector<int>& managerPath, int arrangerNodeID, int regionID) {
    ArrangerNode* arr = undoResolveArrangerNode(p, managerPath, arrangerNodeID);
    if (!arr || !arr->sl) return;
    arr->sl->clearPianoRoll(regionID, false);
}

TogglePianoRollWindowAction::TogglePianoRollWindowAction(Project* p, std::vector<int> managerPath, int arrangerNodeID, int regionID,
                                                         int ewID, float x, float y, float w, float h, int zOrder, bool open) :
        ProjectAction(p, TogglePianoRollWindow),
        managerPath(std::move(managerPath)),
        arrangerNodeID(arrangerNodeID),
        regionID(regionID),
        ewID(ewID),
        x(x), y(y), w(w), h(h),
        zOrder(zOrder),
        open(open) {
    skipInitialDo = true;
    name = open ? "Open Piano Roll" : "Close Piano Roll";
    doAction = [this]() {
        if (this->open)
            openPianoRollWindow(this->p, this->managerPath, this->arrangerNodeID, this->regionID,
                                this->ewID, this->x, this->y, this->w, this->h, this->zOrder);
        else
            closePianoRollWindow(this->p, this->managerPath, this->arrangerNodeID, this->regionID);
    };
    undoAction = [this]() {
        if (this->open)
            closePianoRollWindow(this->p, this->managerPath, this->arrangerNodeID, this->regionID);
        else
            openPianoRollWindow(this->p, this->managerPath, this->arrangerNodeID, this->regionID,
                                this->ewID, this->x, this->y, this->w, this->h, this->zOrder);
    };
}
