#include "UndoManager.h"
#include "GridElement.h"
#include <cmath>
#include "SDL_Events.h"
#include "styles.h"
#include <functional>
#include "Project.h"
#include "NodeProcessor.h"
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
    if (!nm)
        nm = p->processor->guiManager;  // fallback: first use on GUI thread before explicit set
    if (!nm)
        throw std::runtime_error("requireManager: root node manager missing");
    for (int patcherNodeID : path) {
        auto* patcher = dynamic_cast<PatcherNode*>(nm->getNode(static_cast<uint16_t>(patcherNodeID)));
        if (!patcher || !patcher->mainManager)
            throw std::runtime_error("requireManager: invalid patcher in managerPath");
        nm = patcher->mainManager;
    }
    return *nm;
}

void UndoManager::newAction(ProjectAction* pa) {
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
        {"move_node", MoveNode},
        {"add_node", AddNode},
        {"remove_node", RemoveNode},
        {"make_node_connection", MakeNodeConnection},
        {"sever_node_connection", SeverNodeConnection},
        {"create_region", CreateRegion},
        {"delete_region", DeleteRegion},
        {"create_position", CreatePosition},
        {"delete_position", DeletePosition},
        {"move_element_position", MoveElementPosition},
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
        case MoveNode: {
            auto managerPath = params.at("managerPath").get<std::vector<int>>();
            NodeManager& nm = requireManager(head->p, managerPath);
            auto nodeID = params.at("nodeID").get<int>();
            Node* node = (nodeID == 0) ? nm.outNode : nm.getNode(static_cast<uint16_t>(nodeID));
            if (!node)
                throw std::runtime_error("runRegisteredAction move_node: node not found");
            float fromX = node->dstRect.x;
            float fromY = node->dstRect.y;
            pa = new MoveNodeAction(head->p, managerPath, nodeID, fromX, fromY, params.at("toX").get<float>(), params.at("toY").get<float>());
            break;
        }
        case AddArrangerTrack:
            pa = new AddArrangerTrackAction(head->p, params.at("managerPath").get<std::vector<int>>(), params.at("nodeID").get<int>(), params.at("trackType").get<int>());
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
        case MoveNode: {
            pa = new MoveNodeAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>(), j.at("fromX").get<float>(), j.at("fromY").get<float>(),
                j.at("toX").get<float>(), j.at("toY").get<float>());
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
            pa = an;
            break;
        }
        case RemoveNode: {
            auto rn = new RemoveNodeAction(p, j.at("managerPath").get<std::vector<int>>(), j.at("nodeID").get<int>());
            rn->nodeData = j.at("nodeData");
            rn->connectionsData = j.at("connectionsData");
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
        case MoveNode: {
            auto mn = static_cast<MoveNodeAction*>(pa);
            j["managerPath"] = mn->managerPath;
            j["nodeID"] = mn->nodeID;
            j["fromX"] = mn->fromX;
            j["fromY"] = mn->fromY;
            j["toX"] = mn->toX;
            j["toY"] = mn->toY;
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

MoveNodeAction::MoveNodeAction(Project* p, std::vector<int> managerPath, int nodeID, float fromX, float fromY, float toX, float toY) :
        ProjectAction(p, MoveNode),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        fromX(fromX),
        fromY(fromY),
        toX(toX),
        toY(toY) {
    name = "Move Node";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.moveNodeNow(this->nodeID, this->toX, this->toY);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.moveNodeNow(this->nodeID, this->fromX, this->fromY);
    };
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
            auto* restored = nm.addNodeNow(redoNodeSnapshot);
            if (!restored)
                throw std::runtime_error("AddNodeAction::doAction: addNodeNow(json) failed");
            nodeID = restored->id;
            for (auto c : redoConnectionsSnapshot) {
                nm.makeNodeConnectionNow(c.at("srcNodeID").get<int>(), c.at("srcConID").get<int>(), c.at("dstNodeID").get<int>(),
                                         c.at("dstConID").get<int>());
            }
            return;
        }
        auto* node = nm.addNodeNow(static_cast<NodeType>(nodeType), this->x, this->y, nodeID);
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
        }
        nm2.removeNodeNow(this->nodeID);
    };
    undoAction = [this] () {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        auto* restored = nm2.addNodeNow(nodeData);
        if (!restored)
            throw std::runtime_error("RemoveNodeAction::undoAction: addNodeNow failed");
        std::cout << "[DBG_DESER] RemoveNodeAction::undo restored node id=" << restored->id << " managerPath=";
        for (size_t i = 0; i < this->managerPath.size(); ++i) {
            std::cout << this->managerPath[i] << (i + 1 < this->managerPath.size() ? "/" : "");
        }
        if (this->managerPath.empty()) std::cout << "root";
        std::cout << " replayConnections=" << connectionsData.size() << std::endl;
        for (auto c : connectionsData) {
            std::cout << "[DBG_DESER]  undo replay srcNode=" << c["srcNodeID"] << " srcCon=" << c["srcConID"]
                      << " dstNode=" << c["dstNodeID"] << " dstCon=" << c["dstConID"] << std::endl;
            nm2.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
        }
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
