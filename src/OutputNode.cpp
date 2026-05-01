#include "OutputNode.h"
#include "NodeManager.h"
#include "Project.h"
#include "nodes/patcher/patcher.h"
#include "styles.h"
#include <iostream>
#include <string>

void OutputNode::setCoupledPatcher(PatcherNode* p) {
    coupledPatcher = p;
}

size_t OutputNode::countWaveformInputs() const {
    size_t n = 0;
    for (auto* c : inputs.connections) {
        if (c && c->type == DataType::Waveform) ++n;
    }
    return n;
}

size_t OutputNode::countLocalEventInputs() const {
    size_t n = 0;
    for (auto* c : inputs.connections) {
        if (c && c->type == DataType::Events) ++n;
    }
    return n;
}

void OutputNode::addEventOutputChannel() {
    auto* c = new Connection;
    c->type = DataType::Events;
    c->dir = Direction::input;
    inputs.addConnection(c);
    makeConnectionRects();
    if (coupledPatcher) {
        coupledPatcher->setLinkedEventOutputCount(countLocalEventInputs());
        coupledPatcher->nm->markTopologyDirty();
    }
    nm->markTopologyDirty();
}

void OutputNode::removeLastEventOutputChannel() {
    for (int i = static_cast<int>(inputs.connections.size()) - 1; i >= 0; --i) {
        Connection* c = inputs.connections[static_cast<size_t>(i)];
        if (c->type != DataType::Events) continue;
        if (c->is_connected) return;
        inputs.id_pool.releaseID(c->id);
        inputs.ids.erase(c->id);
        inputs.connections.erase(inputs.connections.begin() + i);
        delete c;
        inputs.ids.clear();
        for (size_t j = 0; j < inputs.connections.size(); ++j) {
            inputs.ids[inputs.connections[j]->id] = static_cast<uint16_t>(j);
        }
        makeConnectionRects();
        if (coupledPatcher) {
            coupledPatcher->setLinkedEventOutputCount(countLocalEventInputs());
            coupledPatcher->nm->markTopologyDirty();
        }
        nm->markTopologyDirty();
        return;
    }
}

OutputNode::OutputNode(NodeManager* nm) : Node(0, nm, NodeType::Count) {
    const int initialChannels = nm->managerPath.empty() ? 2 : 0;
    for (int i = 0; i < initialChannels; i++) {
        Connection* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.addConnection(c);
    }

    move(50, 50);
}

void OutputNode::process() {
    int wi = 0;
    for (auto* c : inputs.connections) {
        if (!c || c->type != DataType::Waveform) continue;
        if (wi >= numChannels) return;

        if (!c->is_connected) {
            std::memset(output + wi * bufferSize, 0, bufferSize * sizeof(float));
        } else {
            float* inputBuffer = c->buffer;
            std::memcpy(output + wi * bufferSize, inputBuffer, bufferSize * sizeof(float));
        }
        ++wi;
    }
}

void OutputNode::renderContent(SDL_Renderer* renderer) {
    Node::renderContent(renderer);

    auto drawButton = [renderer](SDL_FRect r, bool plus) {
        SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderRect(renderer, &r);

        float cx = r.x + r.w * 0.5f;
        float cy = r.y + r.h * 0.5f;
        float w = r.w * 0.25f;
        SDL_RenderLine(renderer, cx - w, cy, cx + w, cy);
        if (plus) SDL_RenderLine(renderer, cx, cy - w, cx, cy + w);
    };

    drawButton(addRect, true);
    drawButton(removeRect, false);

    SDL_Color textColor{0, 0, 0, 255};
    std::string label = "ch: " + std::to_string(countWaveformInputs());
    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, label.c_str(), 0, textColor);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_FRect rect{130, 24, static_cast<float>(surf->w), static_cast<float>(surf->h)};
        SDL_RenderTexture(renderer, tex, nullptr, &rect);
        SDL_DestroyTexture(tex);
        SDL_DestroySurface(surf);
    }

    drawButton(evAddRect, true);
    drawButton(evRemoveRect, false);
    std::string evLabel = "ev: " + std::to_string(countLocalEventInputs());
    SDL_Surface* evSurf = TTF_RenderText_Blended(fonts.mainFont, evLabel.c_str(), 0, textColor);
    if (evSurf) {
        SDL_Texture* evTex = SDL_CreateTextureFromSurface(renderer, evSurf);
        SDL_FRect evTextRect{130, 60, static_cast<float>(evSurf->w), static_cast<float>(evSurf->h)};
        SDL_RenderTexture(renderer, evTex, nullptr, &evTextRect);
        SDL_DestroyTexture(evTex);
        SDL_DestroySurface(evSurf);
    }
}

void OutputNode::handleWindowInput(SDL_Event& e) {
    Node::handleWindowInput(e);
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT) return;

    auto inside = [this](const SDL_FRect& r) {
        return msX >= r.x && msX <= r.x + r.w && msY >= r.y && msY <= r.y + r.h;
    };

    if (inside(addRect)) {
        addChannel();
    } else if (inside(removeRect)) {
        removeChannel();
    } else if (inside(evAddRect)) {
        addEventOutputChannel();
    } else if (inside(evRemoveRect)) {
        removeLastEventOutputChannel();
    }
}

void OutputNode::setup() {
}

json OutputNode::serialize() {
    json j;

    j["x"] = dstRect.x;
    j["y"] = dstRect.y;
    j["zoomRatio"] = zoomRatio;
    j["channels"] = countWaveformInputs();
    j["inputConnectionIDs"] = json::array();
    for (auto* c : inputs.connections) {
        if (c->type == DataType::Waveform) {
            j["inputConnectionIDs"].push_back(c->id);
        }
    }

    j["eventInputConnectionIDs"] = json::array();
    for (auto* c : inputs.connections) {
        if (c->type == DataType::Events) {
            j["eventInputConnectionIDs"].push_back(c->id);
        }
    }

    return j;
}

void OutputNode::deSerialize(json j) {
    const int defaultCh = nm->managerPath.empty() ? 2 : 0;
    int targetChannels = j.value("channels", defaultCh);
    std::vector<uint16_t> targetIDs;
    if (j.contains("inputConnectionIDs")) {
        for (auto id : j["inputConnectionIDs"]) {
            targetIDs.push_back(id.get<uint16_t>());
        }
    }
    if (targetIDs.size() != static_cast<size_t>(targetChannels)) {
        targetIDs.clear();
        for (int i = 0; i < targetChannels; ++i) targetIDs.push_back(static_cast<uint16_t>(i));
    }

    for (auto* c : inputs.connections) {
        delete c;
    }
    inputs.connections.clear();
    inputs.ids.clear();
    inputs.id_pool = idManager();

    for (auto id : targetIDs) {
        auto* c = new Connection;
        c->nm = inputs.nm;
        c->id = id;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        c->is_connected = false;
        c->output_connection = c->id;
        c->output_node = inputs.nodeID;
        c->input_connection = -1;
        c->input_node = -1;
        c->events = nullptr;
        c->buffer = nullptr;
        inputs.connections.push_back(c);
        inputs.ids[c->id] = inputs.connections.size() - 1;
        inputs.id_pool.reserveID(c->id);
    }

    std::vector<uint16_t> eventTargetIDs;
    if (j.contains("eventInputConnectionIDs")) {
        for (auto id : j["eventInputConnectionIDs"]) {
            eventTargetIDs.push_back(id.get<uint16_t>());
        }
    }
    for (auto id : eventTargetIDs) {
        auto* c = new Connection;
        c->nm = inputs.nm;
        c->id = id;
        c->type = DataType::Events;
        c->dir = Direction::input;
        c->is_connected = false;
        c->output_connection = c->id;
        c->output_node = inputs.nodeID;
        c->input_connection = -1;
        c->input_node = -1;
        c->events = nullptr;
        c->buffer = nullptr;
        inputs.connections.push_back(c);
        inputs.ids[c->id] = inputs.connections.size() - 1;
        inputs.id_pool.reserveID(c->id);
    }
    makeConnectionRects();

    zoom(j["zoomRatio"].get<float>()/zoomRatio);
    move(j["x"], j["y"]);
}

void OutputNode::addChannel() {
    const size_t pos = countWaveformInputs();
    auto* c = new Connection;
    c->nm = inputs.nm;
    c->id = inputs.id_pool.newID();
    c->type = DataType::Waveform;
    c->dir = Direction::input;
    c->is_connected = false;
    c->output_connection = c->id;
    c->output_node = inputs.nodeID;
    c->input_connection = -1;
    c->input_node = -1;
    c->events = nullptr;
    c->buffer = nullptr;
    inputs.connections.insert(inputs.connections.begin() + static_cast<ptrdiff_t>(pos), c);
    inputs.ids.clear();
    for (size_t j = 0; j < inputs.connections.size(); ++j) {
        inputs.ids[inputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
    if (coupledPatcher) {
        coupledPatcher->setLinkedWaveformChannelCount(countWaveformInputs());
        coupledPatcher->nm->markTopologyDirty();
    }
    nm->markTopologyDirty();
}

void OutputNode::removeChannel() {
    const size_t minWf = nm->managerPath.empty() ? 1 : 0;
    if (countWaveformInputs() <= minWf) return;

    for (int i = static_cast<int>(inputs.connections.size()) - 1; i >= 0; --i) {
        Connection* c = inputs.connections[static_cast<size_t>(i)];
        if (c->type != DataType::Waveform) continue;
        if (c->is_connected) return;

        inputs.id_pool.releaseID(c->id);
        inputs.ids.erase(c->id);
        inputs.connections.erase(inputs.connections.begin() + i);
        delete c;
        inputs.ids.clear();
        for (size_t j = 0; j < inputs.connections.size(); ++j) {
            inputs.ids[inputs.connections[j]->id] = static_cast<uint16_t>(j);
        }
        makeConnectionRects();
        if (coupledPatcher) {
            coupledPatcher->setLinkedWaveformChannelCount(countWaveformInputs());
            coupledPatcher->nm->markTopologyDirty();
        }
        nm->markTopologyDirty();
        return;
    }
}

