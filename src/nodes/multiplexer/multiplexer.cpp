#include "multiplexer.h"
#include "nodes/patcher/patcher.h"
#include "NodeManager.h"
#include <cstring>

MultiplexerNode::MultiplexerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Multiplexer) {
}

MultiplexerNode::~MultiplexerNode() {
    for (auto* p : patchers)
        p->multiplexer = nullptr;
}

void MultiplexerNode::setup() {
    if (!patchers.empty()) return;
    for (int i = 0; i < 8; ++i) {
        float px = x + 50.f + static_cast<float>(i) * 30.f;
        float py = y + h + 40.f;
        auto* n = nm->addNodeNow(NodeType::Patcher, px, py);
        auto* p = static_cast<PatcherNode*>(n);
        if (p) {
            p->multiplexer = this;
            p->visible = false;
            patchers.push_back(p);
        }
    }
    if (!patchers.empty())
        syncPortsFromPatchers();
}

void MultiplexerNode::syncPortsFromPatchers() {
    if (patchers.empty()) return;
    auto* ref = patchers[0];

    // --- waveform outputs ---
    size_t wfOut = 0;
    for (auto* c : ref->outputs.connections)
        if (c && c->type == DataType::Waveform) ++wfOut;
    size_t cur = 0;
    for (auto* c : outputs.connections) if (c && c->type == DataType::Waveform) ++cur;
    while (cur > wfOut) {
        for (int i = (int)outputs.connections.size() - 1; i >= 0; --i) {
            auto* c = outputs.connections[i];
            if (c && c->type == DataType::Waveform) {
                if (c->buffer) { delete[] c->buffer; c->buffer = nullptr; }
                outputs.id_pool.releaseID(c->id);
                outputs.connections.erase(outputs.connections.begin() + i);
                delete c; break;
            }
        }
        --cur;
    }
    while (cur < wfOut) {
        auto* c = new Connection;
        c->nm = outputs.nm;
        c->id = outputs.id_pool.newID();
        c->type = DataType::Waveform;
        c->dir = Direction::output;
        c->is_connected = false;
        c->input_node = id;
        c->input_connection = c->id;
        c->output_node = -1;
        c->output_connection = -1;
        c->bufferSize = bufferSize;
        c->buffer = bufferSize > 0 ? new float[static_cast<size_t>(bufferSize) * static_cast<size_t>(c->numChannels)]() : nullptr;
        c->allocChannels = c->numChannels;
        c->events = nullptr;
        outputs.connections.insert(outputs.connections.begin() + cur, c);
        ++cur;
    }

    // --- event outputs ---
    size_t evOut = ref->outputs.connections.size() - wfOut;
    size_t curEv = outputs.connections.size() - cur;
    while (curEv > evOut) {
        for (int i = (int)outputs.connections.size() - 1; i >= 0; --i) {
            auto* c = outputs.connections[i];
            if (c && c->type == DataType::Events) {
                if (c->events) { delete c->events; c->events = nullptr; }
                outputs.id_pool.releaseID(c->id);
                outputs.connections.erase(outputs.connections.begin() + i);
                delete c; break;
            }
        }
        --curEv;
    }
    while (curEv < evOut) {
        auto* c = new Connection;
        c->type = DataType::Events;
        c->dir = Direction::output;
        outputs.addConnection(c);
        ++curEv;
    }

    // --- waveform inputs ---
    size_t wfIn = 0;
    for (auto* c : ref->inputs.connections)
        if (c && c->type == DataType::Waveform) ++wfIn;
    cur = 0;
    for (auto* c : inputs.connections) if (c && c->type == DataType::Waveform) ++cur;
    while (cur > wfIn) {
        for (int i = (int)inputs.connections.size() - 1; i >= 0; --i) {
            auto* c = inputs.connections[i];
            if (c && c->type == DataType::Waveform) {
                if (c->buffer) { delete[] c->buffer; c->buffer = nullptr; }
                inputs.id_pool.releaseID(c->id);
                inputs.connections.erase(inputs.connections.begin() + i);
                delete c; break;
            }
        }
        --cur;
    }
    while (cur < wfIn) {
        auto* c = new Connection;
        c->nm = inputs.nm;
        c->id = inputs.id_pool.newID();
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        c->output_connection = c->id;
        c->output_node = inputs.nodeID;
        c->input_connection = -1;
        c->input_node = -1;
        c->events = nullptr;
        c->buffer = nullptr;
        c->bufferSize = 0;
        inputs.connections.insert(inputs.connections.begin() + cur, c);
        ++cur;
    }

    // --- event inputs ---
    size_t evIn = ref->inputs.connections.size() - wfIn;
    curEv = inputs.connections.size() - cur;
    while (curEv > evIn) {
        for (int i = (int)inputs.connections.size() - 1; i >= 0; --i) {
            auto* c = inputs.connections[i];
            if (c && c->type == DataType::Events) {
                if (c->events) { delete c->events; c->events = nullptr; }
                inputs.id_pool.releaseID(c->id);
                inputs.connections.erase(inputs.connections.begin() + i);
                delete c; break;
            }
        }
        --curEv;
    }
    while (curEv < evIn) {
        auto* c = new Connection;
        c->type = DataType::Events;
        c->dir = Direction::input;
        inputs.addConnection(c);
        ++curEv;
    }

    inputs.ids.clear();
    for (size_t j = 0; j < inputs.connections.size(); ++j)
        inputs.ids[inputs.connections[j]->id] = static_cast<uint16_t>(j);
    outputs.ids.clear();
    for (size_t j = 0; j < outputs.connections.size(); ++j)
        outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);

    makeConnectionRects();
    nm->markTopologyDirty();
}

void MultiplexerNode::process() {
    int bs = bufferSize;
    if (bs <= 0 || patchers.empty()) return;

    // Zero multiplexer outputs — we'll accumulate into them.
    for (auto* c : outputs.connections) {
        if (!c) continue;
        if (c->type == DataType::Waveform && c->buffer)
            std::memset(c->buffer, 0, static_cast<size_t>(bs) * sizeof(float));
        else if (c->type == DataType::Events && c->events)
            c->events->clear();
    }

    for (auto* p : patchers) {
        // Wire multiplexer inputs → this patcher's inputs (pointer share).
        for (size_t i = 0; i < inputs.connections.size() && i < p->inputs.connections.size(); ++i) {
            auto* src = inputs.connections[i];
            auto* dst = p->inputs.connections[i];
            if (!src || !dst) continue;
            if (src->type == DataType::Waveform) {
                dst->is_connected = src->is_connected;
                dst->buffer = src->buffer;
            } else if (src->type == DataType::Events) {
                dst->is_connected = src->is_connected;
                dst->events = src->events;
            }
        }

        p->process();

        // Accumulate this patcher's outputs → multiplexer outputs.
        for (size_t i = 0; i < outputs.connections.size() && i < p->outputs.connections.size(); ++i) {
            auto* dst = outputs.connections[i];
            auto* src = p->outputs.connections[i];
            if (!src || !dst) continue;
            if (src->type == DataType::Waveform) {
                if (dst->buffer && src->buffer) {
                    for (int j = 0; j < bs; ++j)
                        dst->buffer[j] += src->buffer[j];
                }
            } else if (src->type == DataType::Events) {
                if (dst->events && src->events)
                    dst->events->insert(dst->events->end(), src->events->begin(), src->events->end());
            }
        }
    }
}

// --- chrome (sized for TEX_H=720, node renders ~50px tall on screen) ---
static constexpr float kDotR = 56.f;
static constexpr float kDotSpacing = 140.f;
static constexpr float kBtnW = 130.f;
static constexpr float kChromeY = 70.f;
static constexpr float kChromeX = 80.f;

void MultiplexerNode::renderContent(SDL_Renderer* renderer) {
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];
        vx[0] = 0; vy[0] = 0;
        vx[1] = TEX_W; vy[1] = 0;
        vx[2] = TEX_W; vy[2] = TEX_H;
        vx[3] = 0; vy[3] = TEX_H;
    }

    // Background
    SDL_SetRenderDrawColor(renderer, 26, 30, 38, 255);
    SDL_FRect bg{0, 0, static_cast<float>(TEX_W), static_cast<float>(TEX_H)};
    SDL_RenderFillRect(renderer, &bg);

    // Slot dots (8 fixed slots)
    float cx = kChromeX;
    for (size_t i = 0; i < patchers.size(); ++i) {
        SDL_FRect dot{cx - kDotR, kChromeY - kDotR, kDotR * 2.f, kDotR * 2.f};
        if (i == activeIndex) {
            SDL_SetRenderDrawColor(renderer, 180, 200, 230, 255);
            SDL_RenderFillRect(renderer, &dot);
        } else {
            SDL_SetRenderDrawColor(renderer, 90, 100, 120, 200);
            SDL_RenderRect(renderer, &dot);
        }
        cx += kDotSpacing;
    }

    // Eye toggle
    cx += 40.f;
    SDL_FRect eyeBtn{cx, kChromeY - kBtnW * 0.5f, kBtnW, kBtnW};
    bool vis = activeIndex < patchers.size() && patchers[activeIndex]->visible;
    SDL_SetRenderDrawColor(renderer, vis ? 100 : 50, vis ? 150 : 70, vis ? 200 : 70, 220);
    SDL_RenderFillRect(renderer, &eyeBtn);

    renderParams(renderer);
}

bool MultiplexerNode::handleCustomInput(SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT)
        return false;

    // Slot dots
    float cx = kChromeX;
    for (size_t i = 0; i < patchers.size(); ++i) {
        float dx = msX - cx;
        float dy = msY - kChromeY;
        if (dx * dx + dy * dy < kDotR * kDotR * 1.4f) {
            activeIndex = i;
            return true;
        }
        cx += kDotSpacing;
    }

    // Eye toggle
    cx += 40.f;
    if (msX >= cx && msX <= cx + kBtnW && msY >= kChromeY - kBtnW * 0.5f && msY <= kChromeY + kBtnW * 0.5f) {
        if (activeIndex < patchers.size() && project && project->um) {
            project->um->newAction(new ToggleNodeVisibleAction(project, nm->managerPath, patchers[activeIndex]->id));
        }
        return true;
    }

    return false;
}

void MultiplexerNode::clearCustomTextures() {
}

json MultiplexerNode::extraSerialize() {
    json j;
    j["activeIndex"] = activeIndex;
    j["inputIdPool"] = inputs.id_pool.toJSON();
    j["outputIdPool"] = outputs.id_pool.toJSON();
    j["patchers"] = json::array();
    for (auto* p : patchers) {
        json pj;
        pj["id"] = p->id;
        pj["visible"] = p->visible;
        j["patchers"].push_back(pj);
    }
    return j;
}

void MultiplexerNode::extraDeSerialize(const json& j) {
    for (auto* p : patchers)
        p->multiplexer = nullptr;
    patchers.clear();
    activeIndex = j.value("activeIndex", 0);

    if (j.contains("inputIdPool")) inputs.id_pool.fromJSON(j["inputIdPool"]);
    if (j.contains("outputIdPool")) outputs.id_pool.fromJSON(j["outputIdPool"]);

    if (!j.contains("patchers")) return;

    for (auto& pj : j["patchers"]) {
        uint16_t pid = static_cast<uint16_t>(pj["id"].get<int>());
        auto* patcher = dynamic_cast<PatcherNode*>(nm->getNode(pid));
        patcher->multiplexer = this;
        patcher->visible = pj.value("visible", false);
        patchers.push_back(patcher);
    }
    if (!patchers.empty())
        syncPortsFromPatchers();
}
