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
    j["mappedParams"] = json::object();
    for (size_t pi = 0; pi < params.size(); ++pi) {
        auto* p = params[pi];
        if (!p || !p->mappedConnection) continue;
        j["mappedParams"][std::to_string(pi)] = {
            {"connectionID", p->mappedConnection->id}
        };
    }

    return j;
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

    if (j.contains("mappedParams")) {
        for (auto& [key, val] : j["mappedParams"].items()) {
            size_t pi = std::stoul(key);
            if (pi >= n->params.size()) continue;
            uint16_t connID = val["connectionID"];
            auto* c = new Connection;
            c->nm = n->inputs.nm;
            c->id = connID;
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
            c->label = "Param (mapped)";
            n->inputs.connections.push_back(c);
            n->inputs.id_pool.reserveID(c->id);
            n->inputs.ids[c->id] = static_cast<uint16_t>(n->inputs.connections.size() - 1);
            n->params[pi]->mappedConnection = c;
        }
    }
    n->makeConnectionRects();

    return n;
}

