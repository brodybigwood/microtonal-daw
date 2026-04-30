#include "OutputNode.h"
#include "NodeManager.h"
#include "Project.h"
#include "nodes/patcher/patcher.h"
#include "styles.h"
#include <iostream>
#include <string>
OutputNode::OutputNode(NodeManager* nm) : Node(0, nm, NodeType::Count) {
    for(int i = 0; i < 2; i++) {
        Connection* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.addConnection(c);
    }

    move(50, 50);
}

void OutputNode::process() {
    for(int i = 0; i < numChannels; i++) {
        if(i > inputs.connections.size() - 1) return;
        Connection* c = inputs.connections[i];
        
        if(!c->is_connected) {
            std::memset(output + i*bufferSize, 0, bufferSize * sizeof(float));
            continue;
        }
        
        float* inputBuffer = c->buffer;
        std::memcpy(output + i*bufferSize, inputBuffer, bufferSize * sizeof(float));
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
    std::string label = "ch: " + std::to_string(inputs.connections.size());
    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, label.c_str(), 0, textColor);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_FRect rect{130, 24, static_cast<float>(surf->w), static_cast<float>(surf->h)};
        SDL_RenderTexture(renderer, tex, nullptr, &rect);
        SDL_DestroyTexture(tex);
        SDL_DestroySurface(surf);
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
    }
}

void OutputNode::setup() {
}

json OutputNode::serialize() {
    json j;

    j["x"] = dstRect.x;
    j["y"] = dstRect.y;
    j["zoomRatio"] = zoomRatio;
    j["channels"] = inputs.connections.size();

    return j;
}

void OutputNode::deSerialize(json j) {
    int targetChannels = j.value("channels", 2);
    while (inputs.connections.size() < targetChannels) addChannel();
    while (inputs.connections.size() > targetChannels) removeChannel();
    zoom(j["zoomRatio"].get<float>()/zoomRatio);
    move(j["x"], j["y"]);
}

void OutputNode::addChannel() {
    auto c = new Connection;
    c->type = DataType::Waveform;
    c->dir = Direction::input;
    inputs.addConnection(c);
    makeConnectionRects();

    for (auto n : project->nm->getNodes()) {
        auto p = dynamic_cast<PatcherNode*>(n);
        if (p && p->mainManager == nm) {
            p->ensureOutputChannels(inputs.connections.size());
            break;
        }
    }
}

void OutputNode::removeChannel() {
    if (inputs.connections.size() <= 1) return;
    auto* c = inputs.connections.back();
    if (c->is_connected) return;

    inputs.id_pool.releaseID(c->id);
    inputs.ids.erase(c->id);
    inputs.connections.pop_back();
    delete c;
    makeConnectionRects();

    for (auto n : project->nm->getNodes()) {
        auto p = dynamic_cast<PatcherNode*>(n);
        if (p && p->mainManager == nm) {
            p->ensureOutputChannels(inputs.connections.size());
            break;
        }
    }
}

