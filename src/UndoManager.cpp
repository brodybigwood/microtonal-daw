#include "UndoManager.h"
#include "SDL_Events.h"
#include "styles.h"
#include "Project.h"
#include "NodeProcessor.h"
#include "nodes/nodetypes.h"
#include "NodeManager.h"
#include <unordered_map>

static NodeManager* resolveManager(Project* p, const std::vector<int>& path) {
    if (!p || !p->processor) return nullptr;
    NodeManager* nm = p->processor->getManager();
    if (!nm) return nullptr;
    for (int patcherNodeID : path) {
        auto patcher = dynamic_cast<PatcherNode*>(nm->getNode(static_cast<uint16_t>(patcherNodeID)));
        if (!patcher || !patcher->mainManager) return nullptr;
        nm = patcher->mainManager;
    }
    return nm;
}

const std::unordered_map<std::string, ActionType>& UndoManager::actionRegistry() {
    static const std::unordered_map<std::string, ActionType> reg = {
        {"create_note", CreateNote},
        {"add_arranger_track", AddArrangerTrack},
        {"move_node", MoveNode},
        {"add_node", AddNode},
        {"remove_node", RemoveNode},
        {"make_node_connection", MakeNodeConnection},
        {"sever_node_connection", SeverNodeConnection}
    };
    return reg;
}

std::string UndoManager::actionSchema(const std::string& actionName) {
    if (actionName == "add_node") {
        return R"({"managerPath":[int,...]?,"nodeType":int,"x":float,"y":float})";
    }
    if (actionName == "remove_node") {
        return R"({"managerPath":[int,...]?,"nodeID":int})";
    }
    if (actionName == "make_node_connection" || actionName == "sever_node_connection") {
        return R"({"managerPath":[int,...]?,"srcNodeID":int,"srcConID":int,"dstNodeID":int,"dstConID":int})";
    }
    if (actionName == "move_node") {
        return R"({"managerPath":[int,...]?,"nodeID":int,"toX":float,"toY":float})";
    }
    if (actionName == "add_arranger_track") {
        return R"({"managerPath":[int,...]?,"nodeID":int,"trackType":int})";
    }
    if (actionName == "create_note") {
        return R"({"managerPath":[int,...]?,"nodeID":int,"regionID":int,"start":fract_json,"length":fract_json,"pitch":float})";
    }
    return "{}";
}

bool UndoManager::runRegisteredAction(const std::string& actionName, const json& params, std::string& error) {
    if (!head || !head->p) {
        error = "undo manager not initialized";
        return false;
    }
    if (!params.is_object()) {
        error = "params must be a json object";
        return false;
    }
    auto it = actionRegistry().find(actionName);
    if (it == actionRegistry().end()) {
        error = "unknown action";
        return false;
    }
    ProjectAction* pa = nullptr;
    try {
        switch (it->second) {
            case AddNode:
                if (!params.contains("nodeType") || !params.contains("x") || !params.contains("y")) {
                    error = "missing required keys for add_node";
                    return false;
                }
                pa = new AddNodeAction(head->p, params.value("managerPath", std::vector<int>{}), params["nodeType"], params["x"], params["y"]);
                break;
            case RemoveNode:
                if (!params.contains("nodeID")) {
                    error = "missing required key nodeID";
                    return false;
                }
                pa = new RemoveNodeAction(head->p, params.value("managerPath", std::vector<int>{}), params["nodeID"]);
                break;
            case MakeNodeConnection:
                if (!params.contains("srcNodeID") || !params.contains("srcConID") || !params.contains("dstNodeID") || !params.contains("dstConID")) {
                    error = "missing required keys for make_node_connection";
                    return false;
                }
                pa = new MakeNodeConnectionAction(head->p, params.value("managerPath", std::vector<int>{}), params["srcNodeID"], params["srcConID"], params["dstNodeID"], params["dstConID"]);
                break;
            case SeverNodeConnection:
                if (!params.contains("srcNodeID") || !params.contains("srcConID") || !params.contains("dstNodeID") || !params.contains("dstConID")) {
                    error = "missing required keys for sever_node_connection";
                    return false;
                }
                pa = new SeverNodeConnectionAction(head->p, params.value("managerPath", std::vector<int>{}), params["srcNodeID"], params["srcConID"], params["dstNodeID"], params["dstConID"]);
                break;
            case MoveNode:
                if (!params.contains("nodeID") || !params.contains("toX") || !params.contains("toY")) {
                    error = "missing required keys for move_node";
                    return false;
                }
                {
                    auto managerPath = params.value("managerPath", std::vector<int>{});
                    auto nm = resolveManager(head->p, managerPath);
                    if (!nm) {
                        error = "manager path not found";
                        return false;
                    }
                    auto nodeID = params["nodeID"].get<int>();
                    Node* node = (nodeID == 0) ? nm->outNode : nm->getNode(static_cast<uint16_t>(nodeID));
                    if (!node) {
                        error = "node not found";
                        return false;
                    }
                    float fromX = node->dstRect.x;
                    float fromY = node->dstRect.y;
                    pa = new MoveNodeAction(head->p, managerPath, nodeID, fromX, fromY, params["toX"], params["toY"]);
                }
                break;
            case AddArrangerTrack:
                if (!params.contains("nodeID") || !params.contains("trackType")) {
                    error = "missing required keys for add_arranger_track";
                    return false;
                }
                pa = new AddArrangerTrackAction(head->p, params.value("managerPath", std::vector<int>{}), params["nodeID"], params["trackType"]);
                break;
            case CreateNote: {
                if (!params.contains("nodeID") || !params.contains("regionID") || !params.contains("start") || !params.contains("length") || !params.contains("pitch")) {
                    error = "missing required keys for create_note";
                    return false;
                }
                auto managerPath = params.value("managerPath", std::vector<int>{});
                auto nm = resolveManager(head->p, managerPath);
                auto n = nm ? static_cast<ArrangerNode*>(nm->getNode(params["nodeID"])) : nullptr;
                if (!n || !n->sl) {
                    error = "arranger/songroll not ready for create_note";
                    return false;
                }
                pa = new CreateNoteAction(head->p, managerPath, params["nodeID"], params["regionID"],
                    fract::fromJSON(params["start"]), fract::fromJSON(params["length"]), params["pitch"]);
                break;
            }
            default:
                error = "unsupported action";
                return false;
        }
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (!pa) {
        error = "failed to build action";
        return false;
    }
    newAction(pa);
    return true;
}

ProjectAction* ProjectAction::deSerialize(json j, Project* p) {
    ProjectAction* pa;
    switch (j["type"].get<int>()) {
        case CreateNote: {
            auto managerPath = j.value("managerPath", std::vector<int>{});
            auto nm = resolveManager(p, managerPath);
            auto n = nm ? dynamic_cast<ArrangerNode*>(nm->getNode(j["nodeID"])) : nullptr;
            if (!n || !n->sl || !n->sl->em) {
                pa = new ProjectAction(p, NullAction);
                break;
            }
            auto cn = new CreateNoteAction(
                p, managerPath, j["nodeID"], j["regionID"], fract::fromJSON(j["start"]), fract::fromJSON(j["length"]),
                j["pitch"]);
            cn->noteID = j["noteID"];
            pa = cn;
            break;
        }
        case AddArrangerTrack: {
            auto at = new AddArrangerTrackAction(p, j.value("managerPath", std::vector<int>{}), j["nodeID"], j["trackType"]);
            at->trackID = j["trackID"];
            at->connectionID = j["connectionID"];
            pa = at;
            break;
        }
        case MoveNode: {
            pa = new MoveNodeAction(p, j.value("managerPath", std::vector<int>{}), j["nodeID"], j["fromX"], j["fromY"], j["toX"], j["toY"]);
            break;
        }
        case AddNode: {
            auto an = new AddNodeAction(p, j.value("managerPath", std::vector<int>{}), j["nodeType"], j["x"], j["y"]);
            an->nodeID = j["nodeID"];
            pa = an;
            break;
        }
        case RemoveNode: {
            auto rn = new RemoveNodeAction(p, j.value("managerPath", std::vector<int>{}), j["nodeID"]);
            rn->nodeData = j["nodeData"];
            rn->connectionsData = j["connectionsData"];
            pa = rn;
            break;
        }
        case MakeNodeConnection: {
            pa = new MakeNodeConnectionAction(p, j.value("managerPath", std::vector<int>{}), j["srcNodeID"], j["srcConID"], j["dstNodeID"], j["dstConID"]);
            break;
        }
        case SeverNodeConnection: {
            pa = new SeverNodeConnectionAction(p, j.value("managerPath", std::vector<int>{}), j["srcNodeID"], j["srcConID"], j["dstNodeID"], j["dstConID"]);
            break;
        }
        default:
            pa = new ProjectAction(p, NullAction); // head
            break;
    }

    for (auto jc : j["children"]) {
        auto c = ProjectAction::deSerialize(jc, p);
        pa->newAction(c);
    }

    pa->last_index = j["last_index"];
    pa->name = j["name"];
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
            j["noteID"] = cn->noteID;
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
            break;
        }
        case RemoveNode: {
            auto rn = static_cast<RemoveNodeAction*>(pa);
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
        default:
            break;
}

    json children = json::array();
    for (auto c : pa->children) {
        json jc = ProjectAction::serialize(c);
        children.push_back(jc);
    }

    j["children"] = children;
    j["name"] = pa->name;
    j["last_index"] = pa->last_index;
    return j;
}

bool UndoManager::render(SDL_Renderer* renderer) {
    bool handled = renderAction(renderer, baseRect, head);
    clicked = false;
    return handled;
}

bool UndoManager::renderAction(SDL_Renderer* renderer, SDL_FRect* rect, ProjectAction* pa) {

    bool hovering = false;

    SDL_Color color;

    if (pa == current) {
        if (MouseOn(rect)) {
            color = {100, 200, 100, 255};   
        } else {
            color = {100, 255, 100, 255};
        }
    } else {
        if (MouseOn(rect)) {
            color = {255, 255, 255, 255};
        } else {
            color = {200, 200, 200, 255};
        }
    }


    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_Texture*& texture = pa->texture;
    if (!texture) {
        SDL_Color textColor{0,0,0,255};
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, pa->name.c_str(), 0, textColor);
        texture = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);
    }

    SDL_RenderTexture(renderer, texture, nullptr, rect);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderRect(renderer, rect);

    if (MouseOn(rect)) hovering = true;
    if (hovering && clicked) {
        goTo(pa);
        clicked = false;
    }

    SDL_FRect subRect = *rect;
    subRect.y += subRect.h;

    for (auto c : pa->children) {
        if (renderAction(renderer, &subRect, c)) hovering = true;
        subRect.x += subRect.w;
    }

    return hovering;
}

void UndoManager::goTo(ProjectAction* target) {
    // traverse the tree to some arbitrary action node

    if (!target || !current || !head) return;

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


CreateNoteAction::CreateNoteAction(Project* p, std::vector<int> managerPath, int nodeID, int regionID, fract start, fract length, float pitch) :
        ProjectAction(p, CreateNote),
        managerPath(std::move(managerPath)),
        regionID(regionID),
        start(start),
        length(length),
        pitch(pitch),
        nodeID(nodeID)
        {

        auto nm = resolveManager(p, this->managerPath);
        auto n = nm ? dynamic_cast<ArrangerNode*>(nm->getNode(nodeID)) : nullptr;
        if (!n || !n->sl || !n->sl->em) {
            doAction = [](){};
            undoAction = [](){};
            return;
        }

        doAction = [this, n] () {
            auto region = static_cast<Region*>(n->sl->em->getElement(this->regionID));
            noteID = region->createNote(this->start, this->length, this->pitch);
            name = "Create Note " + std::to_string(noteID) + " " + std::to_string(this->regionID);
        };

        undoAction = [this, n] () {
            auto region = static_cast<Region*>(n->sl->em->getElement(this->regionID));
            region->deleteNote(this->noteID);
        };
}

AddArrangerTrackAction::AddArrangerTrackAction(Project* p, std::vector<int> managerPath, int nodeID, int trackType) :
        ProjectAction(p, AddArrangerTrack),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        trackType(trackType) {
    name = "Add Arranger Track";
    doAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        auto node = static_cast<ArrangerNode*>(nm->getNode(this->nodeID));
        if (!node) return;
        auto track = node->sl->tracks->addTrackNow(static_cast<TrackType>(this->trackType), this->trackID, this->connectionID);
        if (track) {
            this->trackID = track->id;
            if (track->connection) this->connectionID = track->connection->id;
        }
    };
    undoAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        auto node = static_cast<ArrangerNode*>(nm->getNode(this->nodeID));
        if (!node || this->trackID < 0) return;
        node->sl->tracks->removeTrackNow(static_cast<uint16_t>(this->trackID));
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
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        nm->moveNodeNow(this->nodeID, this->toX, this->toY);
    };
    undoAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        nm->moveNodeNow(this->nodeID, this->fromX, this->fromY);
    };
}

AddNodeAction::AddNodeAction(Project* p, std::vector<int> managerPath, int type, float x, float y) :
        ProjectAction(p, AddNode),
        managerPath(std::move(managerPath)),
        nodeType(type),
        x(x),
        y(y) {
    audioThreadAction = true;
    name = "Add Node";
    doAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        auto node = nm->addNodeNow(static_cast<NodeType>(nodeType), this->x, this->y, nodeID);
        if (node) nodeID = node->id;
    };
    undoAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        if (nodeID >= 0) nm->removeNodeNow(nodeID);
    };
}

RemoveNodeAction::RemoveNodeAction(Project* p, std::vector<int> managerPath, int nodeID) :
        ProjectAction(p, RemoveNode),
        managerPath(std::move(managerPath)),
        nodeID(nodeID) {
    audioThreadAction = true;
    name = "Remove Node";
    auto nm = resolveManager(p, this->managerPath);
    if (nm) nm->snapshotNode(nodeID, nodeData, connectionsData);
    doAction = [this] () {
        auto nm2 = resolveManager(this->p, this->managerPath);
        if (!nm2) return;
        nm2->removeNodeNow(this->nodeID);
    };
    undoAction = [this] () {
        auto nm2 = resolveManager(this->p, this->managerPath);
        if (!nm2) return;
        auto restored = nm2->addNodeNow(nodeData);
        if (!restored) return;
        std::cout << "[DBG_DESER] RemoveNodeAction::undo restored node id=" << restored->id << " managerPath=";
        for (size_t i = 0; i < this->managerPath.size(); ++i) {
            std::cout << this->managerPath[i] << (i + 1 < this->managerPath.size() ? "/" : "");
        }
        if (this->managerPath.empty()) std::cout << "root";
        std::cout << " replayConnections=" << connectionsData.size() << std::endl;
        for (auto c : connectionsData) {
            std::cout << "[DBG_DESER]  undo replay srcNode=" << c["srcNodeID"] << " srcCon=" << c["srcConID"]
                      << " dstNode=" << c["dstNodeID"] << " dstCon=" << c["dstConID"] << std::endl;
            nm2->makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
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
    audioThreadAction = true;
    name = "Connect Nodes";
    doAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        nm->makeNodeConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
    undoAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        nm->severConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
}

SeverNodeConnectionAction::SeverNodeConnectionAction(Project* p, std::vector<int> managerPath, int srcNodeID, int srcConID, int dstNodeID, int dstConID) :
        ProjectAction(p, SeverNodeConnection),
        managerPath(std::move(managerPath)),
        srcNodeID(srcNodeID),
        srcConID(srcConID),
        dstNodeID(dstNodeID),
        dstConID(dstConID) {
    audioThreadAction = true;
    name = "Sever Connection";
    doAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        nm->severConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
    undoAction = [this] () {
        auto nm = resolveManager(this->p, this->managerPath);
        if (!nm) return;
        nm->makeNodeConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
}
