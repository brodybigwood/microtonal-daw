#include "paramnode.h"
#include "NodeManager.h"
#include "UndoManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

static constexpr float kRowH = 28.f;
static constexpr float kFootH = 28.f;
static constexpr float kBtnW = 36.f;

static float snapV(float v, float lo, float hi) {
    float step = std::max(1e-6f, (hi - lo) * 0.01f);
    return std::clamp(std::round(v / step) * step, lo, hi);
}
static bool hit(float mx, float my, const SDL_FRect& r) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

ParamNode::ParamNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Param) {
    name = "Param";

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);

    vCount = 4;
    vx = new float[4]{0.f, NODE_W, NODE_W, 0.f};
    vy = new float[4]{0.f, 0.f, NODE_H, NODE_H};
}

void ParamNode::process() {
    if (!out || !out->buffer) return;
    int bs = bufferSize;
    if (bs <= 0) return;
    if (modulators.empty()) {
        for (int i = 0; i < bs; ++i)
            out->buffer[i] = -1.f;
        return;
    }
    for (int i = 0; i < bs; ++i) {
        float v = -1.f;
        for (auto* m : modulators)
            v += (*m)[i];
        out->buffer[i] = std::clamp(v, -1.f, 1.f);
    }
}

// ---- add / remove rows ----

void ParamNode::addModulatorRow() {
    auto* conn = new Connection;
    conn->type = DataType::Waveform;
    conn->dir = Direction::input;
    conn->label = "Mod " + std::to_string(inputs.connections.size() + 1);
    inputs.addConnection(conn);

    auto* mod = new Modulator(conn->buffer, false, generateRect(0, 0, 200, 10), 0.5f, conn);
    modulators.push_back(mod);
    mod->depth.label = conn->label;
    params.push_back(&mod->depth);

    makeConnectionRects();
    nm->markTopologyDirty();
}

void ParamNode::removeModulatorRow(size_t index) {
    if (index >= modulators.size()) return;
    auto* m = modulators[index];
    auto* conn = m->sourceConnection;

    if (conn) {
        if (conn->is_connected)
            nm->severConnectionNow(static_cast<uint16_t>(conn->input_node),
                                   static_cast<uint16_t>(conn->input_connection),
                                   id, conn->id);
        auto it = std::find(inputs.connections.begin(), inputs.connections.end(), conn);
        if (it != inputs.connections.end()) {
            inputs.id_pool.releaseID(conn->id);
            inputs.ids.erase(conn->id);
            inputs.connections.erase(it);
        }
        delete conn;
    }
    auto pit = std::find(params.begin(), params.end(), &m->depth);
    if (pit != params.end()) params.erase(pit);
    delete m;
    modulators.erase(modulators.begin() + static_cast<ptrdiff_t>(index));

    // Fix labels for remaining
    for (size_t i = 0; i < modulators.size(); ++i)
        modulators[i]->depth.label = "Mod " + std::to_string(i + 1);

    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);
    makeConnectionRects();
    nm->markTopologyDirty();
}

// ---- serialisation ----

json ParamNode::extraSerialize() {
    json j;
    j["mods"] = json::array();
    for (auto* m : modulators) {
        if (!m) continue;
        json ji;
        ji["depth"] = m->depth.value;
        ji["centered"] = m->centered;
        if (m->sourceConnection)
            ji["connID"] = static_cast<int>(m->sourceConnection->id);
        j["mods"].push_back(ji);
    }
    return j;
}

void ParamNode::extraDeSerialize(const json& j) {
    if (!j.contains("mods")) return;
    for (auto& ji : j["mods"]) {
        int connID = ji.value("connID", -1);
        Connection* conn = nullptr;
        if (connID >= 0)
            conn = inputs.getConnection(static_cast<uint16_t>(connID));
        if (!conn) {
            conn = new Connection;
            conn->type = DataType::Waveform;
            conn->dir = Direction::input;
            inputs.addConnection(conn);
        }

        auto* mod = new Modulator(conn->buffer, ji.value("centered", false),
                                  generateRect(0, 0, 200, 10), ji.value("depth", 0.5f), conn);
        modulators.push_back(mod);
        mod->depth.label = "Mod " + std::to_string(modulators.size());
        params.push_back(&mod->depth);
    }
}

// ---- rendering ----

void ParamNode::renderContent(SDL_Renderer* R) {
    size_t n = modulators.size();
    float w = static_cast<float>(NODE_W);
    float visH = static_cast<float>(NODE_H);
    float pad = 4.f;

    SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
    { SDL_FRect bg{0, 0, w, visH}; SDL_RenderFillRect(R, &bg); }
    SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
    { SDL_FRect border{0, 0, w, visH}; SDL_RenderRect(R, &border); }

    auto txt = [&](const std::string& s, SDL_FRect r, SDL_Color c) {
        if (!fonts.mainFont) return;
        SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, s.c_str(), 0, c);
        if (!sf) return;
        SDL_Texture* tx = SDL_CreateTextureFromSurface(R, sf);
        if (tx) {
            float sc = std::min(1.f, (r.w - 2.f) / static_cast<float>(sf->w));
            SDL_FRect rr{r.x + (r.w - sf->w * sc) * 0.5f, r.y + (r.h - sf->h * sc) * 0.5f, sf->w * sc, sf->h * sc};
            SDL_RenderTexture(R, tx, nullptr, &rr); SDL_DestroyTexture(tx);
        }
        SDL_DestroySurface(sf);
    };

    float listTop = pad;
    float listH = visH - kFootH - pad;
    float contentH = static_cast<float>(n) * kRowH;
    float maxScroll = std::max(0.f, contentH - listH);
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
    if (scrollOffset < 0.f) scrollOffset = 0.f;

    float rowW = w - pad * 2.f;
    for (size_t i = 0; i < n; ++i) {
        auto* m = modulators[i];
        if (!m) continue;
        float y = listTop + static_cast<float>(i) * kRowH - scrollOffset;
        if (y + kRowH < listTop || y > listTop + listH) continue;

        SDL_FRect rb{pad, y, rowW, kRowH - 2.f};
        SDL_SetRenderDrawColor(R, 240, 240, 240, 255); SDL_RenderFillRect(R, &rb);

        SDL_FRect lb{rb.x + 2.f, rb.y + 2.f, 50.f, rb.h - 4.f};
        txt(m->depth.label, lb, {0,0,0,255});

        SDL_FRect cb{lb.x + lb.w + 4.f, rb.y + 2.f, kBtnW, rb.h - 4.f};
        SDL_SetRenderDrawColor(R, m->centered ? 140 : 200, m->centered ? 220 : 200, 160, 255);
        SDL_RenderFillRect(R, &cb);
        txt("C", cb, {0,0,0,255});

        SDL_FRect rm{rb.x + rb.w - kBtnW - 2.f, rb.y + 2.f, kBtnW, rb.h - 4.f};
        SDL_SetRenderDrawColor(R, 235, 150, 150, 255); SDL_RenderFillRect(R, &rm);
        txt("X", rm, {0,0,0,255});

        float sx = cb.x + cb.w + 6.f;
        float sw = rm.x - sx - 6.f;
        if (sw < 10.f) sw = 10.f;
        SDL_FRect sl{sx, rb.y + (rb.h - 8.f) * 0.5f, sw, 8.f};
        SDL_SetRenderDrawColor(R, 180, 180, 180, 255); SDL_RenderFillRect(R, &sl);
        SDL_SetRenderDrawColor(R, 100, 100, 100, 255); SDL_RenderRect(R, &sl);

        if (m->centered) {
            float cx = sl.x + sl.w * 0.5f;
            SDL_SetRenderDrawColor(R, 100, 100, 100, 255);
            SDL_RenderLine(R, cx, sl.y - 3.f, cx, sl.y + sl.h + 3.f);
        }

        float lo = -1.f, hi = 1.f;
        float t = std::clamp((m->depth.value - lo) / std::max(1e-6f, hi - lo), 0.f, 1.f);
        SDL_FRect kn{sl.x + t * sl.w - 3.f, sl.y - 3.f, 6.f, sl.h + 6.f};
        SDL_SetRenderDrawColor(R, 80, 120, 220, 255); SDL_RenderFillRect(R, &kn);

        std::ostringstream ds; ds << std::fixed << std::setprecision(2) << m->depth.value;
        SDL_FRect dv{rm.x - 42.f, rb.y + 2.f, 38.f, rb.h - 4.f};
        txt(ds.str(), dv, {0,0,0,255});
    }

    SDL_FRect add{pad, visH - kFootH + 4.f, 80.f, kFootH - 8.f};
    SDL_SetRenderDrawColor(R, 170, 210, 255, 255); SDL_RenderFillRect(R, &add);
    SDL_SetRenderDrawColor(R, 80, 80, 80, 255); SDL_RenderRect(R, &add);
    txt("+ Mod", add, {0,0,0,255});
}

// ---- input handling ----

bool ParamNode::handleCustomInput(SDL_Event& e) {
    size_t n = modulators.size();
    float w = static_cast<float>(NODE_W);
    float visH = static_cast<float>(NODE_H);
    float pad = 4.f;
    float listTop = pad;
    float listH = visH - kFootH - pad;
    float rowW = w - pad * 2.f;

    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT)
        return false;

    SDL_FRect add{pad, visH - kFootH + 4.f, 80.f, kFootH - 8.f};
    if (hit(msX, msY, add)) {
        if (project && project->um) {
            std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
            auto* pa = new ParamNodeAddModRowUndoAction(project, std::move(mp), static_cast<int>(id));
            project->um->newAction(pa);
        } else {
            addModulatorRow();
        }
        return true;
    }

    for (size_t i = 0; i < n; ++i) {
        auto* m = modulators[i];
        if (!m) continue;
        float y = listTop + static_cast<float>(i) * kRowH - scrollOffset;
        if (y + kRowH < listTop || y > listTop + listH) continue;
        SDL_FRect rb{pad, y, rowW, kRowH - 2.f};
        SDL_FRect lb{rb.x + 2.f, rb.y + 2.f, 50.f, rb.h - 4.f};
        SDL_FRect cb{lb.x + lb.w + 4.f, rb.y + 2.f, kBtnW, rb.h - 4.f};
        SDL_FRect rm{rb.x + rb.w - kBtnW - 2.f, rb.y + 2.f, kBtnW, rb.h - 4.f};
        float sx = cb.x + cb.w + 6.f, sw = rm.x - sx - 6.f;
        if (sw < 10.f) sw = 10.f;
        SDL_FRect sl{sx, rb.y + (rb.h - 8.f) * 0.5f, sw, 8.f};

        if (hit(msX, msY, cb)) {
            bool oldC = m->centered;
            float oldD = m->depth.value;
            m->centered = !m->centered;
            if (project && project->um) {
                std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                auto* pa = new ParamNodeToggleCenteredUndoAction(project, std::move(mp), static_cast<int>(id), i, oldC, m->centered, oldD, m->depth.value);
                project->um->newAction(pa);
            }
            return true;
        }
        if (hit(msX, msY, rm)) {
            if (project && project->um) {
                std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                auto* pa = new ParamNodeRemoveModRowUndoAction(project, std::move(mp), static_cast<int>(id), i);
                project->um->newAction(pa);
            } else {
                removeModulatorRow(i);
                makeConnectionRects();
                nm->markTopologyDirty();
            }
            return true;
        }
        if (hit(msX, msY, sl)) {
            auto* m = modulators[i];
            if (m && m->depth.mappedConnection && m->depth.mappedConnection->is_connected)
                return true;
            draggingIndex = static_cast<int>(i);
            oldDepth = m->depth.value;
            return true;
        }
    }

    return false;
}

void ParamNode::handleWindowInput(SDL_Event& e) {
    size_t n = modulators.size();
    float w = static_cast<float>(NODE_W);
    float visH = static_cast<float>(NODE_H);
    float pad = 4.f;
    float listTop = pad;
    float listH = visH - kFootH - pad;
    float rowW = w - pad * 2.f;

    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        float contentH = static_cast<float>(n) * kRowH;
        float maxScroll = std::max(0.f, contentH - listH);
        scrollOffset -= e.wheel.y * 20.f;
        scrollOffset = std::clamp(scrollOffset, 0.f, maxScroll);
        return;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        if (draggingIndex >= 0 && draggingIndex < static_cast<int>(n)) {
            auto* m = modulators[static_cast<size_t>(draggingIndex)];
            if (m && m->depth.value != oldDepth && project && project->um) {
                std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                std::vector<size_t> path = {static_cast<size_t>(draggingIndex)};
                auto* pa = new SetParamValueUndoAction(project, std::move(mp), static_cast<int>(id), path, oldDepth, m->depth.value, "Depth");
                project->um->newAction(pa);
            }
        }
        draggingIndex = -1;
    }

    if ((e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) &&
        draggingIndex >= 0 && draggingIndex < static_cast<int>(n)) {
        size_t i = static_cast<size_t>(draggingIndex);
        auto* m = modulators[i];
        if (!m) return;
        if (m->depth.mappedConnection && m->depth.mappedConnection->is_connected) return;
        float y = listTop + static_cast<float>(i) * kRowH - scrollOffset;
        SDL_FRect rb{pad, y, rowW, kRowH - 2.f};
        SDL_FRect lb{rb.x + 2.f, rb.y + 2.f, 50.f, rb.h - 4.f};
        SDL_FRect cb{lb.x + lb.w + 4.f, rb.y + 2.f, kBtnW, rb.h - 4.f};
        SDL_FRect rm{rb.x + rb.w - kBtnW - 2.f, rb.y + 2.f, kBtnW, rb.h - 4.f};
        float sx = cb.x + cb.w + 6.f, sw = rm.x - sx - 6.f;
        if (sw < 10.f) sw = 10.f;
        float norm = std::clamp((msX - sx) / sw, 0.f, 1.f);
        float lo = -1.f, hi = 1.f;
        m->depth.value = snapV(lo + norm * (hi - lo), lo, hi);
    }
}

void ParamNode::clearCustomTextures() {}
