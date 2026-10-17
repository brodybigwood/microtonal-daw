#include "ControlApi.h"

#include "AudioManager.h"
#include "ControlDocs.h"
#include "Node.h"
#include "NodeEditor.h"
#include "NodeManager.h"
#include "NodeProcessor.h"
#include "Project.h"
#include "Settings.h"
#include "UndoInternal.h"
#include "UndoManager.h"
#include "WindowHandler.h"
#include "nodes/nodetypes.h"
#include "nodes/vst/vstnode.h"

#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

Project* currentProject() {
    WindowHandler* wh = WindowHandler::instance();
    return wh ? wh->project : nullptr;
}

json ok(const json& id, json result) {
    json r;
    r["id"] = id;
    r["ok"] = true;
    r["result"] = std::move(result);
    return r;
}

json err(const json& id, const std::string& code, const std::string& message) {
    json r;
    r["id"] = id;
    r["ok"] = false;
    r["error"] = json{{"code", code}, {"message", message}};
    return r;
}

std::vector<int> readManagerPath(const json& args) {
    std::vector<int> path;
    if (args.contains("managerPath") && args["managerPath"].is_array())
        for (const auto& v : args["managerPath"])
            path.push_back(v.get<int>());
    return path;
}

std::string typeNameFor(const Node* n) {
    if (n->id == 0) return "Output";
    if (n->id == 1) return "Input";
    const int t = static_cast<int>(n->nodeType);
    if (t >= 0 && t < static_cast<int>(NodeType::Count)) return NodeTypeStr[t];
    return "Unknown";
}

json paramJson(Node* n, size_t index) {
    Parameter* p = n->params[index];
    json j;
    j["index"] = index;
    j["label"] = p->label.empty() ? ("Param " + std::to_string(index)) : p->label;
    j["value"] = p->value;
    j["defaultValue"] = p->defaultValue;
    j["mapped"] = (p->mappedConnection != nullptr);
    j["lockMapping"] = p->lockMapping;
    return j;
}

json nodeSummary(Node* n) {
    json j;
    j["id"] = n->id;
    j["name"] = n->name;
    j["nodeType"] = static_cast<int>(n->nodeType);
    j["typeName"] = typeNameFor(n);
    j["x"] = n->dstRect.x;
    j["y"] = n->dstRect.y;
    j["w"] = n->dstRect.w;
    j["h"] = n->dstRect.h;
    j["paramCount"] = n->params.size();
    return j;
}

json portJson(Connection* c) {
    json j;
    j["id"] = c->id;
    j["label"] = c->label;
    j["dataType"] = static_cast<int>(c->type);
    j["connected"] = c->is_connected;
    j["numChannels"] = c->numChannels;
    j["minChannels"] = c->minChannels;
    if (c->is_connected) {
        j["peerNode"] = (c->dir == Direction::input) ? c->input_node : c->output_node;
        j["peerConnection"] = (c->dir == Direction::input) ? c->input_connection : c->output_connection;
    }
    return j;
}

json nodeDetail(Node* n) {
    json j = nodeSummary(n);
    json params = json::array();
    for (size_t i = 0; i < n->params.size(); ++i)
        params.push_back(paramJson(n, i));
    j["params"] = std::move(params);
    json ins = json::array(), outs = json::array();
    for (auto* c : n->inputs.connections) ins.push_back(portJson(c));
    for (auto* c : n->outputs.connections) outs.push_back(portJson(c));
    j["inputs"] = std::move(ins);
    j["outputs"] = std::move(outs);
    return j;
}

void collectManagers(NodeManager& nm, const std::vector<int>& path, json& out) {
    out.push_back(json{{"managerPath", path}, {"nodeCount", nm.getNodes().size()}});
    for (Node* n : nm.getNodes()) {
        std::vector<int> childPath = path;
        childPath.push_back(static_cast<int>(n->id));
        if (auto* pat = dynamic_cast<PatcherNode*>(n)) {
            if (pat->mainManager) collectManagers(*pat->mainManager, childPath, out);
        } else if (auto* mux = dynamic_cast<MultiplexerNode*>(n)) {
            for (auto* mp : mux->patchers) {
                if (!mp || !mp->mainManager) continue;
                std::vector<int> muxPath = childPath;
                muxPath.push_back(static_cast<int>(mp->id));
                collectManagers(*mp->mainManager, muxPath, out);
            }
        }
    }
}

json tempoCurveJson(const AutomationCurve& c) {
    json j;
    j["valueRangeMin"] = c.valueRangeMin;
    j["valueRangeMax"] = c.valueRangeMax;
    j["points"] = json::array();
    for (const auto& pt : c.points) {
        json jp;
        jp["timeVec"] = json::array();
        for (const auto& pr : pt.timeVec)
            jp["timeVec"].push_back(json::array({pr.first, pr.second}));
        jp["v"] = pt.v;
        jp["shape"] = static_cast<int>(pt.shape.type);
        jp["shapeParam"] = pt.shape.param;
        j["points"].push_back(std::move(jp));
    }
    return j;
}

json handleQuery(Project* p, const json& req) {
    const std::string q = req.value("query", "");
    const json args = req.value("args", json::object());

    if (q == "node_types") {
        json out = json::array();
        for (int i = 0; i < static_cast<int>(NodeType::Count); ++i)
            out.push_back(json{{"name", NodeTypeStr[i]}, {"type", i}});
        return out;
    }
    if (q == "audio") {
        AudioManager* am = AudioManager::instance();
        return json{
            {"sampleRate", am->sampleRate},
            {"bufferSize", am->bufferSize},
            {"inputChannels", am->inputChannels},
            {"outputChannels", am->outputChannels},
            {"latency", am->latency},
        };
    }
    if (q == "settings") return Settings::instance().toJSON();
    if (q == "undo") return p->um->serialize();
    if (q == "transport") {
        return json{
            {"isPlaying", p->isPlaying},
            {"beatPosition", p->beatPosition},
            {"timeSeconds", p->timeSeconds},
            {"effectiveBeatPosition", p->effectiveBeatPosition},
            {"effectiveTime", p->effectiveTime},
            {"playHeadBeat", p->playHeadBeat},
            {"playHeadStart", p->playHeadStart},
            {"tempoCurve", tempoCurveJson(p->tempoCurve)},
        };
    }
    if (q == "managers") {
        json out = json::array();
        if (p->processor && p->processor->guiGraph)
            collectManagers(*p->processor->guiGraph, {}, out);
        return out;
    }

    // Everything below addresses a specific manager.
    NodeManager& nm = requireManager(p, readManagerPath(args));

    if (q == "graph") return nm.serialize();

    if (q == "nodes") {
        json out = json::array();
        out.push_back(nodeSummary(nm.getNode(0))); // output node
        out.push_back(nodeSummary(nm.getNode(1))); // input node
        for (Node* n : nm.getNodes()) out.push_back(nodeSummary(n));
        return out;
    }
    if (q == "connections") {
        json g = nm.serialize();
        return g.contains("connections") ? g["connections"] : json::array();
    }
    if (q == "vst_params") {
#ifndef __EMSCRIPTEN__
        const int nodeID = args.value("nodeID", -1);
        auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
        if (!vst || !vst->plugin)
            throw std::runtime_error("vst_params: node has no loaded plugin");
        json out = json::array();
        const int count = vst->plugin->getParameterCount();
        for (int i = 0; i < count; ++i) {
            const int pid = vst->plugin->getParameterID(i);
            if (pid < 0) continue;
            out.push_back(json{{"index", i}, {"id", pid},
                               {"name", vst->plugin->getParameterName(i)},
                               {"value", vst->plugin->getParameterValue(pid)}});
        }
        return out;
#else
        throw std::runtime_error("vst_params: VST is not available in this build");
#endif
    }
    if (q == "embedded_windows")
        return nm.ne ? nm.ne->serializeEmbeddedWindows() : json::array();
    if (q == "piano_rolls")
        return nm.ne ? nm.ne->serializeOpenPianoRolls(nm.managerPath) : json::array();
    if (q == "arranger" || q == "tracks" || q == "elements" || q == "region") {
        const int nodeID = args.value("nodeID", -1);
        auto* arr = dynamic_cast<ArrangerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
        if (!arr) throw std::runtime_error("query '" + q + "' needs an arranger nodeID");
        if (q == "tracks") return arr->tracks ? arr->tracks->toJSON() : json::array();
        if (q == "elements") return arr->elements ? arr->elements->toJSON() : json::array();
        if (q == "region") {
            if (!arr->elements) throw std::runtime_error("arranger has no element manager");
            auto* el = arr->elements->getElement(static_cast<uint16_t>(args.value("regionID", -1)));
            if (!el) throw std::runtime_error("region not found");
            return el->toJSON();
        }
        json out;
        out["tracks"] = arr->tracks ? arr->tracks->toJSON() : json::array();
        out["elements"] = arr->elements ? arr->elements->toJSON() : json::array();
        return out;
    }
    if (q == "node" || q == "params") {
        const int nodeID = args.value("nodeID", -1);
        Node* n = nm.getNode(static_cast<uint16_t>(nodeID));
        if (!n) throw std::runtime_error("node not found: " + std::to_string(nodeID));
        if (q == "params") {
            json out = json::array();
            for (size_t i = 0; i < n->params.size(); ++i) out.push_back(paramJson(n, i));
            return out;
        }
        return nodeDetail(n);
    }
    throw std::runtime_error("unknown query: " + q);
}

} // namespace

namespace {

json dispatch(const json& req) {
    const json id = req.contains("id") ? req["id"] : json();
    const std::string op = req.value("op", "");

    if (op == "ping") return ok(id, json{{"pong", true}, {"control", 1}});

    Project* p = currentProject();
    if (!p) return err(id, "no_project", "no project available");

    if (op == "help") {
        json actions = json::object(), queries = json::object(), ops = json::object();
        for (const auto& kv : actionDocs().items()) actions[kv.key()] = kv.value().value("summary", "");
        for (const auto& kv : queryDocs().items())  queries[kv.key()] = kv.value().value("summary", "");
        for (const auto& kv : opDocs().items())     ops[kv.key()]     = kv.value().value("summary", "");
        json out{{"actions", actions}, {"queries", queries}, {"ops", ops}};
        if (req.value("concepts", true)) out["concepts"] = controlConcepts();
        return ok(id, out);
    }

    if (op == "list_actions") {
        json names = json::array();
        for (const auto& kv : UndoManager::actionRegistry()) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        json out = json::array();
        for (const auto& n : names) {
            json e{{"name", n}};
            if (actionDocs().contains(n.get<std::string>()))
                e["summary"] = actionDocs()[n.get<std::string>()].value("summary", "");
            out.push_back(std::move(e));
        }
        return ok(id, json{{"actions", out}});
    }

    if (op == "describe_action") {
        const std::string name = req.value("action", "");
        if (name.empty()) return err(id, "bad_request", "missing 'action'");
        json out;
        if (actionDocs().contains(name)) {
            out = actionDocs()[name];
        } else {
            out = json{{"summary", ""}, {"params", json::object()}, {"example", ""}};
        }
        out["name"] = name;
        out["known"] = UndoManager::actionRegistry().count(name) > 0;
        const std::string legacy = UndoManager::actionSchema(name);
        if (!legacy.empty()) {
            json parsed = json::parse(legacy, nullptr, false);
            if (!parsed.is_discarded()) out["schema"] = parsed;
        }
        return ok(id, out);
    }

    if (op == "list_queries") {
        json out = json::array();
        for (const auto& kv : queryDocs().items())
            out.push_back(json{{"name", kv.key()}, {"summary", kv.value().value("summary", "")}});
        return ok(id, json{{"queries", out}});
    }

    if (op == "describe_query") {
        const std::string name = req.value("query", "");
        if (name.empty()) return err(id, "bad_request", "missing 'query'");
        if (!queryDocs().contains(name))
            return err(id, "unknown_query", "unknown query: " + name);
        json out = queryDocs()[name];
        out["name"] = name;
        return ok(id, out);
    }

    if (op == "describe_op") {
        const std::string name = req.value("describe", "");
        if (name.empty()) return err(id, "bad_request", "missing 'describe' (the op to look up)");
        if (!opDocs().contains(name))
            return err(id, "unknown_op", "unknown op: " + name);
        json out = opDocs()[name];
        out["name"] = name;
        return ok(id, out);
    }

    if (op == "query") return ok(id, handleQuery(p, req));

    if (op == "action") {
        const std::string name = req.value("action", "");
        if (name.empty()) return err(id, "bad_request", "missing 'action'");
        const json args = req.value("args", json::object());
        std::string error;
        if (!p->um->runRegisteredAction(name, args, error))
            return err(id, "execution_failed", error);
        return ok(id, json{{"applied", name}});
    }

    if (op == "undo") {
        p->um->undo();
        return ok(id, json{{"undone", true}});
    }

    if (op == "redo") {
        if (req.contains("index")) p->um->redo(req["index"].get<int>());
        else p->um->redo();
        return ok(id, json{{"redone", true}});
    }

    if (op == "goto") {
        if (!req.contains("version") || !req["version"].is_array())
            return err(id, "bad_request", "goto needs a 'version' array");
        ProjectAction* a = p->um->head;
        for (const auto& v : req["version"]) {
            const int idx = v.get<int>();
            if (!a || idx < 0 || static_cast<size_t>(idx) >= a->children.size())
                return err(id, "not_found", "version path does not exist in the undo tree");
            a = a->children[static_cast<size_t>(idx)];
        }
        p->um->goTo(a);
        return ok(id, json{{"version", a->version()}});
    }

    if (op == "save") {
        if (req.contains("path") && req["path"].is_string())
            p->filepath = req["path"].get<std::string>();
        if (p->filepath.empty())
            return err(id, "bad_request", "no 'path' given and project has no filepath yet");
        p->save();
        return ok(id, json{{"saved", p->filepath}});
    }

    if (op == "load") {
        const std::string path = req.value("path", "");
        if (path.empty()) return err(id, "bad_request", "load needs a 'path'");
        p->load(path);
        return ok(id, json{{"loaded", path}});
    }

    if (op == "transport") {
        if (req.contains("play") && req["play"].get<bool>()) {
            if (!p->isPlaying) p->togglePlaying();
        } else if (req.contains("stop") && req["stop"].get<bool>()) {
            if (p->isPlaying) p->togglePlaying();
        }
        if (req.contains("seekBeat")) p->seekBeat(req["seekBeat"].get<double>());
        return ok(id, json{{"isPlaying", p->isPlaying}, {"beatPosition", p->beatPosition},
                           {"timeSeconds", p->timeSeconds}});
    }

    if (op == "shutdown") {
        WindowHandler::instance()->running = false;
        return ok(id, json{{"shutdown", true}});
    }

    return err(id, "unknown_op", "unknown op: " + op);
}

} // namespace

std::string ControlApi::handle(const std::string& line) {
    json req;
    try {
        req = json::parse(line);
    } catch (const std::exception& e) {
        return err(json(), "bad_json", e.what()).dump();
    }
    if (!req.is_object()) return err(json(), "bad_request", "request must be a JSON object").dump();

    const json id = req.contains("id") ? req["id"] : json();
    try {
        return dispatch(req).dump();
    } catch (const std::exception& e) {
        return err(id, "execution_failed", e.what()).dump();
    } catch (...) {
        return err(id, "execution_failed", "unknown error").dump();
    }
}
