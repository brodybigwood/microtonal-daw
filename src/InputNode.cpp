#include "InputNode.h"
#include "UndoManager.h"
#include "NodeManager.h"
#include "nodes/patcher/patcher.h"
#include "styles.h"
#include <cstring>
#include <string>
#include <algorithm>
#include <limits>

namespace {
using Quad = std::array<SDL_FPoint, 4>;

bool pointInQuad(float x, float y, const Quad& q) {
    bool inside = false;
    for (size_t i = 0, j = q.size() - 1; i < q.size(); j = i++) {
        const auto& pi = q[i];
        const auto& pj = q[j];
        const bool intersect = ((pi.y > y) != (pj.y > y)) &&
                               (x < (pj.x - pi.x) * (y - pi.y) / ((pj.y - pi.y) + 0.000001f) + pi.x);
        if (intersect) inside = !inside;
    }
    return inside;
}

SDL_Color hoverTint(SDL_Color c, bool hovered) {
    if (!hovered) return c;
    auto bump = [](uint8_t v) { return static_cast<uint8_t>(std::min(255, static_cast<int>(v) + 35)); };
    return SDL_Color{bump(c.r), bump(c.g), bump(c.b), c.a};
}

void fillQuad(SDL_Renderer* renderer, const Quad& q, SDL_Color fill) {
    float vx[4]{q[0].x, q[1].x, q[2].x, q[3].x};
    float vy[4]{q[0].y, q[1].y, q[2].y, q[3].y};
    filledPolygonRGBA(renderer, vx, vy, 4, fill.r, fill.g, fill.b, fill.a);
}

void drawButtonIcon(SDL_Renderer* renderer, const Quad& q, bool plus) {
    float cx = 0.0f, cy = 0.0f;
    for (const auto& p : q) {
        cx += p.x;
        cy += p.y;
    }
    cx *= 0.25f;
    cy *= 0.25f;
    float minX = q[0].x, maxX = q[0].x, minY = q[0].y, maxY = q[0].y;
    for (const auto& p : q) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    float w = std::min(maxX - minX, maxY - minY) * 0.18f;
    const int t = 3;
    for (int i = -t; i <= t; ++i) SDL_RenderLine(renderer, cx - w, cy + i, cx + w, cy + i);
    if (plus) for (int i = -t; i <= t; ++i) SDL_RenderLine(renderer, cx + i, cy - w, cx + i, cy + w);
}

void drawThickSegment(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, int t) {
    float nx = y2 - y1;
    float ny = -(x2 - x1);
    float len = std::max(1.0f, std::sqrt(nx * nx + ny * ny));
    nx /= len;
    ny /= len;
    for (int i = -t; i <= t; ++i) {
        SDL_RenderLine(renderer, x1 + nx * i, y1 + ny * i, x2 + nx * i, y2 + ny * i);
    }
}

void drawThickSegmentInward(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, int t, const Quad& zone) {
    float nx = y2 - y1;
    float ny = -(x2 - x1);
    float len = std::max(1.0f, std::sqrt(nx * nx + ny * ny));
    nx /= len;
    ny /= len;
    const float mx = 0.5f * (x1 + x2);
    const float my = 0.5f * (y1 + y2);
    float sign = pointInQuad(mx + nx * 1.0f, my + ny * 1.0f, zone) ? 1.0f : -1.0f;
    for (int i = 0; i <= t; ++i) {
        SDL_RenderLine(renderer, x1 + nx * sign * i, y1 + ny * sign * i, x2 + nx * sign * i, y2 + ny * sign * i);
    }
}
}

void InputNode::setCoupledPatcher(PatcherNode* p) {
    coupledPatcher = p;
}

void InputNode::placeDefaultByWindowSize(float windowW, float windowH) {
    (void)windowH;
    if (!shouldAutoPlaceFromWindow) return;
    move(windowW * 0.5f, 50.0f);
    shouldAutoPlaceFromWindow = false;
}

size_t InputNode::countWaveformOutputs() const {
    size_t n = 0;
    for (auto* c : outputs.connections) {
        if (c && c->type == DataType::Waveform) ++n;
    }
    return n;
}

size_t InputNode::totalWaveformChannels() const {
    size_t n = 0;
    for (auto* c : outputs.connections) {
        if (c && c->type == DataType::Waveform) n += static_cast<size_t>(c->numChannels);
    }
    return n;
}

size_t InputNode::countLocalEventOutputs() const {
    size_t n = 0;
    for (auto* c : outputs.connections) {
        if (c && c->type == DataType::Events) ++n;
    }
    return n;
}

void InputNode::addEventOutputSocket() {
    auto* c = new Connection;
    c->type = DataType::Events;
    c->dir = Direction::output;
    outputs.addConnection(c);
    makeConnectionRects();
    if (coupledPatcher) {
        coupledPatcher->setLinkedEventInputCount(countLocalEventOutputs());
        coupledPatcher->nm->markTopologyDirty();
    }
    nm->markTopologyDirty();
}

void InputNode::removeLastEventOutputSocket() {
    for (int i = static_cast<int>(outputs.connections.size()) - 1; i >= 0; --i) {
        Connection* c = outputs.connections[static_cast<size_t>(i)];
        if (c->type != DataType::Events) continue;
        if (c->is_connected) return;
        if (c->events) {
            delete c->events;
            c->events = nullptr;
        }
        outputs.id_pool.releaseID(c->id);
        outputs.ids.erase(c->id);
        outputs.connections.erase(outputs.connections.begin() + i);
        delete c;
        outputs.ids.clear();
        for (size_t j = 0; j < outputs.connections.size(); ++j) {
            outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
        }
        makeConnectionRects();
        if (coupledPatcher) {
            coupledPatcher->setLinkedEventInputCount(countLocalEventOutputs());
            coupledPatcher->nm->markTopologyDirty();
        }
        nm->markTopologyDirty();
        return;
    }
}

InputNode::InputNode(NodeManager* nm) : Node(1, nm, NodeType::Count) {
    const int initialChannels = nm->managerPath.empty() ? 2 : 0;
    for (int i = 0; i < initialChannels; i++) {
        Connection* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::output;
        outputs.addConnection(c);
    }

    move(0.0f, 0.0f);
}

void InputNode::process() {
    int wi = 0;
    for (auto* c : outputs.connections) {
        if (!c || c->type != DataType::Waveform) continue;
        if (wi >= numChannels) return;

        if (!c->buffer) continue;

        if (!input) {
            std::memset(c->channel(0), 0, static_cast<size_t>(bufferSize) * sizeof(float));
        } else {
            float* src = input + static_cast<size_t>(wi) * static_cast<size_t>(bufferSize);
            std::memcpy(c->channel(0), src, static_cast<size_t>(bufferSize) * sizeof(float));
        }
        ++wi;
    }
}

void InputNode::renderContent(SDL_Renderer* renderer) {
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];

        // Opposite of OutputNode/Node default trapezoid: narrow top, wide bottom.
        vx[0] = 300;
        vx[1] = TEX_W - 300;
        vx[2] = TEX_W;
        vx[3] = 0;

        vy[0] = 0;
        vy[1] = 0;
        vy[2] = TEX_H;
        vy[3] = TEX_H;
    }
    const float zoneTop = TEX_H - 100.0f;
    const float zoneH = 100.0f;
    const float rowH = zoneH * 0.5f;
    const float y0 = zoneTop;
    const float y1 = zoneTop + rowH;
    const float y2 = zoneTop + zoneH;
    auto leftAt = [](float y) { return (300.0f / TEX_H) * (TEX_H - y); };
    auto rightAt = [&](float y) { return TEX_W - leftAt(y); };
    auto split = [](float l, float r) { return l + (r - l) * 0.5f; };

    const float l0 = leftAt(y0), r0 = rightAt(y0), m0 = split(l0, r0);
    const float l1 = leftAt(y1), r1 = rightAt(y1), m1 = split(l1, r1);
    const float l2 = leftAt(y2), r2 = rightAt(y2), m2 = split(l2, r2);

    // Input node: top row minus, bottom row plus (flipped from output). Left=event, right=waveform.
    evRemoveQuad = Quad{{{l0, y0}, {m0, y0}, {m1, y1}, {l1, y1}}};
    removeQuad = Quad{{{m0, y0}, {r0, y0}, {r1, y1}, {m1, y1}}};
    evAddQuad = Quad{{{l1, y1}, {m1, y1}, {m2, y2}, {l2, y2}}};
    addQuad = Quad{{{m1, y1}, {r1, y1}, {r2, y2}, {m2, y2}}};

    Node::renderContent(renderer);

    const SDL_Color eventColor{140, 220, 140, 255};
    const SDL_Color waveformColor{255, 170, 170, 255};
    fillQuad(renderer, evAddQuad, hoverTint(eventColor, pointInQuad(msX, msY, evAddQuad)));
    fillQuad(renderer, evRemoveQuad, hoverTint(eventColor, pointInQuad(msX, msY, evRemoveQuad)));
    fillQuad(renderer, removeQuad, hoverTint(waveformColor, pointInQuad(msX, msY, removeQuad)));
    fillQuad(renderer, addQuad, hoverTint(waveformColor, pointInQuad(msX, msY, addQuad)));

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    const int lineT = 4;
    Quad zone{{{l0, y0}, {r0, y0}, {r2, y2}, {l2, y2}}};
    drawThickSegmentInward(renderer, l0, y0, r0, y0, lineT, zone); // zone top
    drawThickSegment(renderer, l1, y1, r1, y1, lineT); // +/- split
    drawThickSegmentInward(renderer, l2, y2, r2, y2, lineT, zone); // zone bottom
    drawThickSegmentInward(renderer, l0, y0, l2, y2, lineT, zone); // left edge
    drawThickSegmentInward(renderer, r0, y0, r2, y2, lineT, zone); // right edge
    drawThickSegment(renderer, m0, y0, m2, y2, lineT); // event/wf split

    drawButtonIcon(renderer, evAddQuad, true);
    drawButtonIcon(renderer, evRemoveQuad, false);
    drawButtonIcon(renderer, removeQuad, false);
    drawButtonIcon(renderer, addQuad, true);
}

void InputNode::handleWindowInput(SDL_Event& e) {
    Node::handleWindowInput(e);
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT) return;
    if (!project || !project->um) return;

    if (pointInQuad(msX, msY, addQuad)) {
        project->um->newAction(new IoPortChannelAction(project, IoPortChannelOp::InputAddWaveform, nm->managerPath, 0, 0));
    } else if (pointInQuad(msX, msY, removeQuad)) {
        uint16_t rid = 0;
        size_t ridx = 0;
        if (!nm->peekRemovableInputWaveform(&rid, &ridx)) return;
        project->um->newAction(new IoPortChannelAction(project, IoPortChannelOp::InputRemoveWaveform, nm->managerPath, rid, ridx));
    } else if (pointInQuad(msX, msY, evAddQuad)) {
        project->um->newAction(new IoPortChannelAction(project, IoPortChannelOp::InputAddEvent, nm->managerPath, 0, 0));
    } else if (pointInQuad(msX, msY, evRemoveQuad)) {
        uint16_t rid = 0;
        size_t ridx = 0;
        if (!nm->peekRemovableInputEvent(&rid, &ridx)) return;
        project->um->newAction(new IoPortChannelAction(project, IoPortChannelOp::InputRemoveEvent, nm->managerPath, rid, ridx));
    }
}

void InputNode::setup() {
}

bool InputNode::blocksDoubleClick(float x, float y) const {
    return pointInQuad(x, y, addQuad) ||
           pointInQuad(x, y, removeQuad) ||
           pointInQuad(x, y, evAddQuad) ||
           pointInQuad(x, y, evRemoveQuad);
}

json InputNode::serialize() {
    json j;

    j["x"] = dstRect.x;
    j["y"] = dstRect.y;
    j["zoomRatio"] = zoomRatio;
    j["channels"] = totalWaveformChannels();
    j["outputConnectionIDs"] = json::array();
    for (auto* c : outputs.connections) {
        if (c->type == DataType::Waveform) {
            j["outputConnectionIDs"].push_back(c->id);
        }
    }

    j["eventOutputConnectionIDs"] = json::array();
    for (auto* c : outputs.connections) {
        if (c->type == DataType::Events) {
            j["eventOutputConnectionIDs"].push_back(c->id);
        }
    }

    return j;
}

void InputNode::deSerialize(json j) {
    const int defaultCh = nm->managerPath.empty() ? 2 : 0;
    int targetChannels = j.value("channels", defaultCh);
    std::vector<uint16_t> targetIDs;
    if (j.contains("outputConnectionIDs")) {
        for (auto id : j["outputConnectionIDs"]) {
            targetIDs.push_back(id.get<uint16_t>());
        }
    }
    if (targetIDs.empty()) {
        for (int i = 0; i < targetChannels; ++i) {
            targetIDs.push_back(static_cast<uint16_t>(i));
        }
    }

    for (auto* c : outputs.connections) {
        delete c;
    }
    outputs.connections.clear();
    outputs.ids.clear();
    outputs.id_pool = idManager();

    for (size_t i = 0; i < targetIDs.size(); ++i) {
        auto* c = new Connection;
        c->nm = outputs.nm;
        c->id = targetIDs[i];
        c->type = DataType::Waveform;
        c->dir = Direction::output;
        c->is_connected = false;
        c->input_node = outputs.nodeID;
        c->input_connection = c->id;
        c->output_node = -1;
        c->output_connection = -1;
        c->events = nullptr;
        c->buffer = nullptr;
        c->bufferSize = 0;
        c->numChannels = 1;
        outputs.connections.push_back(c);
        outputs.ids[c->id] = outputs.connections.size() - 1;
        outputs.id_pool.reserveID(c->id);
    }

    std::vector<uint16_t> eventTargetIDs;
    if (j.contains("eventOutputConnectionIDs")) {
        for (auto id : j["eventOutputConnectionIDs"]) {
            eventTargetIDs.push_back(id.get<uint16_t>());
        }
    }
    for (auto id : eventTargetIDs) {
        auto* c = new Connection;
        c->nm = outputs.nm;
        c->id = id;
        c->type = DataType::Events;
        c->dir = Direction::output;
        c->is_connected = false;
        c->input_node = outputs.nodeID;
        c->input_connection = c->id;
        c->output_node = -1;
        c->output_connection = -1;
        c->buffer = nullptr;
        c->bufferSize = 0;
        c->events = new std::vector<Event>;
        outputs.connections.push_back(c);
        outputs.ids[c->id] = outputs.connections.size() - 1;
        outputs.id_pool.reserveID(c->id);
    }
    makeConnectionRects();

    zoom(j["zoomRatio"].get<float>() / zoomRatio);
    move(j["x"], j["y"]);
    shouldAutoPlaceFromWindow = false;
}

void InputNode::addWaveformOutputChannel() {
    const size_t pos = countWaveformOutputs();
    auto* c = new Connection;
    c->nm = outputs.nm;
    c->id = outputs.id_pool.newID();
    c->type = DataType::Waveform;
    c->dir = Direction::output;
    c->is_connected = false;
    c->input_node = outputs.nodeID;
    c->input_connection = c->id;
    c->output_node = -1;
    c->output_connection = -1;
    c->events = nullptr;
    c->buffer = nullptr;
    c->bufferSize = outputs.bufferSize;
    outputs.connections.insert(outputs.connections.begin() + static_cast<ptrdiff_t>(pos), c);
    outputs.ids.clear();
    for (size_t j = 0; j < outputs.connections.size(); ++j) {
        outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
    if (coupledPatcher) {
        coupledPatcher->setLinkedWaveformInputCount(countWaveformOutputs());
        coupledPatcher->nm->markTopologyDirty();
    }
    nm->markTopologyDirty();
}

void InputNode::removeLastWaveformOutputChannel() {
    const size_t minWf = nm->managerPath.empty() ? 1 : 0;
    if (countWaveformOutputs() <= minWf) return;

    for (int i = static_cast<int>(outputs.connections.size()) - 1; i >= 0; --i) {
        Connection* c = outputs.connections[static_cast<size_t>(i)];
        if (c->type != DataType::Waveform) continue;
        if (c->is_connected) return;

        if (c->buffer) {
            delete[] c->buffer;
            c->buffer = nullptr;
        }
        outputs.id_pool.releaseID(c->id);
        outputs.ids.erase(c->id);
        outputs.connections.erase(outputs.connections.begin() + i);
        delete c;
        outputs.ids.clear();
        for (size_t j = 0; j < outputs.connections.size(); ++j) {
            outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
        }
        makeConnectionRects();
        if (coupledPatcher) {
            coupledPatcher->setLinkedWaveformInputCount(countWaveformOutputs());
            coupledPatcher->nm->markTopologyDirty();
        }
        nm->markTopologyDirty();
        return;
    }
}

bool InputNode::peekLastRemovableWaveformOutput(uint16_t* outId, size_t* outIndex) const {
    const size_t minWf = nm->managerPath.empty() ? 1 : 0;
    if (countWaveformOutputs() <= minWf) return false;
    for (int i = static_cast<int>(outputs.connections.size()) - 1; i >= 0; --i) {
        Connection* c = outputs.connections[static_cast<size_t>(i)];
        if (c->type != DataType::Waveform) continue;
        if (c->is_connected) return false;
        if (outId) *outId = c->id;
        if (outIndex) *outIndex = static_cast<size_t>(i);
        return true;
    }
    return false;
}

bool InputNode::removeWaveformOutputById(uint16_t id) {
    const size_t minWf = nm->managerPath.empty() ? 1 : 0;
    if (countWaveformOutputs() <= minWf) return false;
    const uint16_t idx = outputs.getIndex(id);
    if (idx == std::numeric_limits<uint16_t>::max()) return false;
    if (idx >= outputs.connections.size()) return false;
    Connection* c = outputs.connections[idx];
    if (!c || c->type != DataType::Waveform || c->is_connected) return false;

    if (c->buffer) {
        delete[] c->buffer;
        c->buffer = nullptr;
    }
    outputs.id_pool.releaseID(c->id);
    outputs.ids.erase(c->id);
    outputs.connections.erase(outputs.connections.begin() + static_cast<ptrdiff_t>(idx));
    delete c;
    outputs.ids.clear();
    for (size_t j = 0; j < outputs.connections.size(); ++j) {
        outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
    if (coupledPatcher) {
        coupledPatcher->setLinkedWaveformInputCount(countWaveformOutputs());
        coupledPatcher->nm->markTopologyDirty();
    }
    nm->markTopologyDirty();
    return true;
}

void InputNode::insertWaveformOutputChannelAt(size_t index, uint16_t id) {
    outputs.id_pool.reserveID(id);
    auto* c = new Connection;
    c->nm = outputs.nm;
    c->id = id;
    c->type = DataType::Waveform;
    c->dir = Direction::output;
    c->is_connected = false;
    c->input_node = outputs.nodeID;
    c->input_connection = c->id;
    c->output_node = -1;
    c->output_connection = -1;
    c->events = nullptr;
    c->buffer = nullptr;
    c->bufferSize = bufferSize;
    c->allocChannels = c->numChannels;
    if (bufferSize > 0) {
        c->buffer = new float[static_cast<size_t>(bufferSize) * static_cast<size_t>(c->numChannels)];
        std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(c->numChannels) * sizeof(float));
    }
    outputs.connections.insert(outputs.connections.begin() + static_cast<ptrdiff_t>(index), c);
    outputs.ids.clear();
    for (size_t j = 0; j < outputs.connections.size(); ++j) {
        outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
    if (coupledPatcher) {
        coupledPatcher->setLinkedWaveformInputCount(countWaveformOutputs());
        coupledPatcher->nm->markTopologyDirty();
    }
    nm->markTopologyDirty();
}

bool InputNode::peekLastRemovableEventOutput(uint16_t* outId, size_t* outIndex) const {
    for (int i = static_cast<int>(outputs.connections.size()) - 1; i >= 0; --i) {
        Connection* c = outputs.connections[static_cast<size_t>(i)];
        if (c->type != DataType::Events) continue;
        if (c->is_connected) return false;
        if (outId) *outId = c->id;
        if (outIndex) *outIndex = static_cast<size_t>(i);
        return true;
    }
    return false;
}

bool InputNode::removeEventOutputById(uint16_t id) {
    const uint16_t idx = outputs.getIndex(id);
    if (idx == std::numeric_limits<uint16_t>::max()) return false;
    if (idx >= outputs.connections.size()) return false;
    Connection* c = outputs.connections[idx];
    if (!c || c->type != DataType::Events || c->is_connected) return false;

    if (c->events) {
        delete c->events;
        c->events = nullptr;
    }
    outputs.id_pool.releaseID(c->id);
    outputs.ids.erase(c->id);
    outputs.connections.erase(outputs.connections.begin() + static_cast<ptrdiff_t>(idx));
    delete c;
    outputs.ids.clear();
    for (size_t j = 0; j < outputs.connections.size(); ++j) {
        outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
    if (coupledPatcher) {
        coupledPatcher->setLinkedEventInputCount(countLocalEventOutputs());
        coupledPatcher->nm->markTopologyDirty();
    }
    nm->markTopologyDirty();
    return true;
}

void InputNode::insertEventOutputChannelAt(size_t index, uint16_t id) {
    outputs.id_pool.reserveID(id);
    auto* c = new Connection;
    c->nm = outputs.nm;
    c->id = id;
    c->type = DataType::Events;
    c->dir = Direction::output;
    c->is_connected = false;
    c->input_node = outputs.nodeID;
    c->input_connection = c->id;
    c->output_node = -1;
    c->output_connection = -1;
    c->buffer = nullptr;
    c->bufferSize = 0;
    c->events = new std::vector<Event>;
    outputs.connections.insert(outputs.connections.begin() + static_cast<ptrdiff_t>(index), c);
    outputs.ids.clear();
    for (size_t j = 0; j < outputs.connections.size(); ++j) {
        outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
    if (coupledPatcher) {
        coupledPatcher->setLinkedEventInputCount(countLocalEventOutputs());
        coupledPatcher->nm->markTopologyDirty();
    }
    nm->markTopologyDirty();
}
