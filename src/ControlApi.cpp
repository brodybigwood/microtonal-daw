#include "ControlApi.h"

#include "AudioManager.h"
#include "ControlDocs.h"
#include "JsonBytes.h"
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
#include "nodes/sampler/sampler.h"
#include "nodes/vst/vstnode.h"
#include "PreferencesExpandedWindow.h"
#include "UndoTreeExpandedWindow.h"
#include "ArrangerExpandedWindow.h"
#include "PatcherExpandedWindow.h"
#include "PianoRollWindow.h"
#include "WindowManager.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
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

// ---- screen capture: PNG encoded into memory, answered after the render pass ----

struct MemIO { std::vector<uint8_t> data; Sint64 pos = 0; };

Sint64 SDLCALL memSize(void* u) { return static_cast<Sint64>(static_cast<MemIO*>(u)->data.size()); }
Sint64 SDLCALL memSeek(void* u, Sint64 off, SDL_IOWhence whence) {
    auto* m = static_cast<MemIO*>(u);
    const Sint64 end = static_cast<Sint64>(m->data.size());
    const Sint64 base = whence == SDL_IO_SEEK_SET ? 0 : (whence == SDL_IO_SEEK_CUR ? m->pos : end);
    const Sint64 np = base + off;
    if (np < 0) return -1;
    m->pos = np;
    return np;
}
size_t SDLCALL memRead(void*, void*, size_t, SDL_IOStatus*) { return 0; }
size_t SDLCALL memWrite(void* u, const void* ptr, size_t size, SDL_IOStatus*) {
    auto* m = static_cast<MemIO*>(u);
    if (m->pos + static_cast<Sint64>(size) > static_cast<Sint64>(m->data.size()))
        m->data.resize(static_cast<size_t>(m->pos + static_cast<Sint64>(size)));
    std::memcpy(m->data.data() + m->pos, ptr, size);
    m->pos += static_cast<Sint64>(size);
    return size;
}
bool SDLCALL memFlush(void*, SDL_IOStatus*) { return true; }
bool SDLCALL memClose(void*) { return true; }

bool rendererToPng(SDL_Renderer* r, std::vector<uint8_t>& png, int& w, int& h) {
    if (!r) return false;
    SDL_Surface* surf = SDL_RenderReadPixels(r, nullptr);
    if (!surf) return false;
    w = surf->w;
    h = surf->h;
    MemIO mem;
    SDL_IOStreamInterface iface{};
    SDL_INIT_INTERFACE(&iface);
    iface.size = memSize;
    iface.seek = memSeek;
    iface.read = memRead;
    iface.write = memWrite;
    iface.flush = memFlush;
    iface.close = memClose;
    SDL_IOStream* io = SDL_OpenIO(&iface, &mem);
    const bool saved = io && IMG_SavePNG_IO(surf, io, true);
    SDL_DestroySurface(surf);
    if (!saved) return false;
    png = std::move(mem.data);
    return !png.empty();
}

struct PendingCapture { int fd = -1; json id; std::string target; };
std::vector<PendingCapture> g_pendingCaptures;   // GUI thread only

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
    if (q == "windows") {
        json expanded = json::array();
        WindowManager* wm = p->processor ? p->processor->getWindowManager() : nullptr;
        if (wm) {
            for (const auto& up : wm->windows()) {
                ExpandedWindow* w = up.get();
                if (!w) continue;
                int ww = 0, hh = 0;
                if (w->window) SDL_GetWindowSize(w->window, &ww, &hh);
                expanded.push_back(json{{"id", w->id}, {"sdlWindowID", w->sdlWindowID},
                                        {"title", w->title}, {"visible", w->visible},
                                        {"width", ww}, {"height", hh}});
            }
        }
        json main = json::object();
        if (p->window) {
            int mw = 0, mh = 0;
            SDL_GetWindowSize(p->window, &mw, &mh);
            main = json{{"target", "main"}, {"title", "NodeProcessorHost"},
                        {"sdlWindowID", SDL_GetWindowID(p->window)},
                        {"width", mw}, {"height", mh}};
        }
        return json{{"main", main}, {"expanded", expanded}};
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
        json created;
        if (!p->um->runRegisteredAction(name, args, error, &created))
            return err(id, "execution_failed", error);
        json out{{"applied", name}};
        // The ids of anything the action created, so the caller never has to guess.
        if (created.is_object() && !created.empty()) out["created"] = std::move(created);
        return ok(id, std::move(out));
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

    if (op == "window") {
        WindowManager* wm = p->processor ? p->processor->getWindowManager() : nullptr;
        if (!wm) return err(id, "internal", "no window manager");

        if (req.contains("close")) {
            const std::string t = req["close"].get<std::string>();
            ExpandedWindow* ew = wm->findByTitle(t.c_str());
            if (!ew) ew = wm->findById(std::atoi(t.c_str()));
            if (!ew) return err(id, "not_found", "no window titled/id '" + t + "'");
            wm->removeWindow(ew);
            return ok(id, json{{"closed", t}});
        }

        auto opened = [&](ExpandedWindow* ew, const std::string& title) -> json {
            if (!ew) return err(id, "internal", "failed to open " + title);
            ew->show();
            return ok(id, json{{"opened", title}, {"id", ew->id}, {"sdlWindowID", ew->sdlWindowID}});
        };
        auto reuse = [&](const std::string& title) -> ExpandedWindow* {
            ExpandedWindow* ew = wm->findByTitle(title.c_str());
            if (ew) { ew->show(); SDL_RaiseWindow(ew->window); }
            return ew;
        };
        auto nodeByID = [&](int nid) -> Node* {
            return (p->processor && p->processor->guiGraph)
                ? p->processor->guiGraph->getNode(static_cast<uint16_t>(nid)) : nullptr;
        };

        const std::string kind = req.value("open", "");
        if (kind == "preferences") {
            if (auto* ew = reuse("Preferences")) return opened(ew, "Preferences");
            auto w = std::make_unique<PreferencesExpandedWindow>();
            ExpandedWindow* ew = wm->addWindow(std::move(w), 500, 500, "Preferences");
            if (ew) ew->setPosition(120, 120);
            return opened(ew, "Preferences");
        }
        if (kind == "undo_tree") {
            if (auto* ew = reuse("Undo Tree")) return opened(ew, "Undo Tree");
            auto w = std::make_unique<UndoTreeExpandedWindow>(p);
            ExpandedWindow* ew = wm->addWindow(std::move(w), 480, 640, "Undo Tree");
            if (ew) ew->setPosition(120, 120);
            return opened(ew, "Undo Tree");
        }
        if (kind == "arranger" || kind == "patcher") {
            Node* n = nodeByID(req.value("nodeID", -1));
            if (kind == "arranger") {
                auto* arr = dynamic_cast<ArrangerNode*>(n);
                if (!arr) return err(id, "not_found", "arranger node not found");
                std::string title = "Arranger##" + std::to_string(arr->id);
                if (auto* ew = reuse(title)) return opened(ew, title);
                auto w = std::make_unique<ArrangerExpandedWindow>(arr, p);
                return opened(wm->addWindow(std::move(w), 1200, 800, title.c_str()), title);
            }
            auto* pat = dynamic_cast<PatcherNode*>(n);
            if (!pat) return err(id, "not_found", "patcher node not found");
            std::string title = "Patcher##" + std::to_string(pat->id);
            if (auto* ew = reuse(title)) return opened(ew, title);
            auto w = std::make_unique<PatcherExpandedWindow>(pat, p);
            return opened(wm->addWindow(std::move(w), 1200, 800, title.c_str()), title);
        }
        if (kind == "sample_editor") {
            auto* sn = dynamic_cast<SamplerNode*>(nodeByID(req.value("nodeID", -1)));
            if (!sn) return err(id, "not_found", "sampler node not found");
            sn->openSampleEditor(static_cast<size_t>(req.value("sampleIndex", 0)));
            return opened(wm->findByTitle("Sample Editor"), "Sample Editor");
        }
        if (kind == "piano_roll") {
            auto* arr = dynamic_cast<ArrangerNode*>(nodeByID(req.value("nodeID", -1)));
            if (!arr) return err(id, "not_found", "arranger node not found");
            arr->ensureSongRoll();
            if (!arr->sl || !arr->elements) return err(id, "internal", "arranger song roll unavailable");
            auto* region = dynamic_cast<Region*>(
                arr->elements->getElement(static_cast<uint16_t>(req.value("regionID", -1))));
            if (!region) return err(id, "not_found", "region not found");
            arr->sl->createPianoRoll(region, /*createUndo=*/true);
            return opened(wm->findByTitle("Piano Roll"), "Piano Roll");
        }
        return err(id, "bad_request", "unknown window to open: " + kind +
                       " (preferences, undo_tree, arranger, patcher, sample_editor, piano_roll)");
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

std::string ControlApi::handle(const std::string& line, int clientFd) {
    json req;
    try {
        req = json::parse(line);
    } catch (const std::exception& e) {
        return err(json(), "bad_json", e.what()).dump();
    }
    if (!req.is_object()) return err(json(), "bad_request", "request must be a JSON object").dump();

    const json id = req.contains("id") ? req["id"] : json();

    // A capture has to read the frame after it is drawn, so defer the reply to
    // flushCaptures(), which runs between the render and present passes.
    if (req.value("op", "") == "capture") {
        if (clientFd < 0)
            return err(id, "bad_request", "capture is only available over the control socket").dump();
        g_pendingCaptures.push_back({clientFd, id, req.value("target", std::string("main"))});
        return {};
    }

    try {
        return dispatch(req).dump();
    } catch (const std::exception& e) {
        return err(id, "execution_failed", e.what()).dump();
    } catch (...) {
        return err(id, "execution_failed", "unknown error").dump();
    }
}

std::vector<std::pair<int, std::string>> ControlApi::flushCaptures(SDL_Renderer* mainRenderer,
                                                                   WindowManager* wm) {
    std::vector<std::pair<int, std::string>> out;
    if (g_pendingCaptures.empty()) return out;

    std::vector<PendingCapture> pending;
    pending.swap(g_pendingCaptures);

    for (const auto& pc : pending) {
        SDL_Renderer* r = nullptr;
        if (pc.target == "main") {
            r = mainRenderer;
        } else if (wm) {
            if (ExpandedWindow* ew = wm->findByTitle(pc.target.c_str()))
                r = ew->renderer;
            else if (ExpandedWindow* e2 = wm->findById(std::atoi(pc.target.c_str())))
                r = e2->renderer;
        }
        if (!r) {
            out.push_back({pc.fd, err(pc.id, "not_found",
                "capture target has no renderer: " + pc.target).dump()});
            continue;
        }
        std::vector<uint8_t> png;
        int w = 0, h = 0;
        if (!rendererToPng(r, png, w, h)) {
            out.push_back({pc.fd, err(pc.id, "capture_failed",
                "could not read pixels from " + pc.target + ": " + SDL_GetError()).dump()});
            continue;
        }
        json res{{"target", pc.target}, {"format", "png"}, {"width", w}, {"height", h},
                 {"data", jsonBytesEncode(png)}};
        out.push_back({pc.fd, ok(pc.id, std::move(res)).dump()});
    }
    return out;
}
