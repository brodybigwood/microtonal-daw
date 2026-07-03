#include "Node.h"
#include "NodeManager.h"
#include "SDL_Events.h"
#include "ContextMenu.h"
#include <iostream>
#include "NodeEditor.h"
#include "WindowHandler.h"
#include "Preferences.h"
#include "Settings.h"
#include "UndoManager.h"
#include <cstring>
#include <limits>
#include <sstream>
#include <iomanip>
#include "styles.h"

// Node JSON (de)serialization, incl. modulator trees (split from Node.cpp).

namespace {
} // namespace

static void serializeModulators(const std::vector<Modulator*>& modulators, json& arr) {
    for (auto* m : modulators) {
        if (!m) continue;
        json jm;
        jm["centered"] = m->centered;
        jm["depth"] = m->depth.value;
        jm["sourceConnectionID"] = m->sourceConnection ? m->sourceConnection->id : -1;
        jm["sourceLabel"] = (m->sourceConnection && !m->sourceConnection->label.empty())
            ? m->sourceConnection->label
            : "";
        if (!m->depth.modulators.empty()) {
            json nested = json::array();
            serializeModulators(m->depth.modulators, nested);
            jm["depthModulators"] = nested;
        }
        arr.push_back(jm);
    }
}

json Node::serialize() {
    json j;

    j["id"] = id;
    j["name"] = name;
    j["zoomRatio"] = kDefaultZoomRatio;
    j["nodeType"] = nodeType;
    j["x"] = dstRect.x;
    j["y"] = dstRect.y;
    j["ewid"] = EmbeddedWindow::id;
    j["extra"] = extraSerialize();
    j["params"] = json::array();
    for (auto* p : params) {
        j["params"].push_back(p ? p->value : 0.0f);
    }
    j["modulators"] = json::array();
    for (size_t pi = 0; pi < params.size(); ++pi) {
        auto* p = params[pi];
        if (!p) continue;
        json arr = json::array();
        serializeModulators(p->modulators, arr);
        for (auto& jm : arr) {
            jm["paramIndex"] = pi;
            j["modulators"].push_back(jm);
        }
    }

    return j;
}

static void deserializeModulators(Parameter* p, const json& arr, Node* n) {
    for (const auto& jm : arr) {
        int sourceID = jm.value("sourceConnectionID", -1);
        if (sourceID < 0) continue;

        auto* c = new Connection;
        c->nm = n->inputs.nm;
        c->id = static_cast<uint16_t>(sourceID);
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        c->is_connected = false;
        c->output_connection = c->id;
        c->output_node = n->inputs.nodeID;
        c->input_connection = -1;
        c->input_node = -1;
        c->events = nullptr;
        c->buffer = nullptr;
        c->bufferSize = 0;
        c->label = jm.value("sourceLabel", std::string{});

        n->inputs.connections.push_back(c);
        n->inputs.id_pool.reserveID(c->id);
        n->inputs.ids[c->id] = static_cast<uint16_t>(n->inputs.connections.size() - 1);

        const bool centered = jm.value("centered", true);
        const float depth = jm.value("depth", 0.5f);
        auto* mod = new Modulator(c->buffer, centered, generateRect(0, 0, 200, 10), depth, c);
        p->addModulator(mod);

        if (jm.contains("depthModulators")) {
            deserializeModulators(&mod->depth, jm["depthModulators"], n);
        }
    }
}

Node* Node::deSerialize(json j, NodeManager* nm, bool skipExtra) {
    int id = j["id"];

    Node* n = byType(j["nodeType"].get<NodeType>(), id, nm);

    n->name = j["name"];

    n->move(j["x"], j["y"]);
    if (j.contains("ewid")) n->EmbeddedWindow::id = j["ewid"];

    if (!skipExtra)
        n->extraDeSerialize(j["extra"]);

    if (j.contains("params")) {
        const auto& jp = j["params"];
        for (size_t i = 0; i < n->params.size() && i < jp.size(); ++i) {
            n->params[i]->value = jp[i].get<float>();
        }
    }

    if (j.contains("modulators")) {
        // Group modulators by paramIndex
        std::map<size_t, json> byParam;
        for (const auto& jm : j["modulators"]) {
            size_t pi = jm.value("paramIndex", static_cast<size_t>(0));
            if (!byParam.count(pi)) byParam[pi] = json::array();
            byParam[pi].push_back(jm);
        }
        for (auto& [pi, arr] : byParam) {
            if (pi >= n->params.size()) continue;
            auto* p = n->params[pi];
            if (!p) continue;
            deserializeModulators(p, arr, n);
        }
    }
    n->makeConnectionRects();

    return n;
}

