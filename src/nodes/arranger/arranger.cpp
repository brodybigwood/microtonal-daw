#include "arranger.h"
#include <iostream>
#include "NodeManager.h"
#include "NodeEditor.h"
#include "PianoRoll.h"

ArrangerNode::ArrangerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Arranger) {
    slRect = new SDL_FRect{0, 0, TEX_W, TEX_H};
    sl = new SongRoll(slRect, &slDetached, nm->ne, nm->project, this);
}

void ArrangerNode::process() {
    sl->tracks->process(nullptr, bufferSize);
    sl->em->process(bufferSize);
}

void ArrangerNode::setup() {
}

bool ArrangerNode::handleCustomInput(SDL_Event& e) {
    sl->mouseX = msX - slRect->x;
    sl->mouseY = msY - slRect->y;

    return sl->handleInput(e);
}

void ArrangerNode::renderContent(SDL_Renderer* renderer) {
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];

        vx[0] = slRect->x;
        vx[1] = slRect->x + slRect->w;
        vx[2] = slRect->x + slRect->w;
        vx[3] = slRect->x;

        vy[0] = slRect->y;
        vy[1] = slRect->y;
        vy[2] = slRect->y + slRect->h;
        vy[3] = slRect->y + slRect->h;
    }

    sl->tick();
    renderParams(renderer);
}      

void ArrangerNode::clearCustomTextures() {
    sl->clearTextures();
    sl->renderer = renderer;
    sl->window = window;
    sl->generateTextures();
}

void ArrangerNode::renderPresent() {
    if (detached) SDL_RenderPresent(renderer);
    if (sl->pianoRoll && sl->pianoRollDetached) SDL_RenderPresent(sl->pianoRoll->renderer);
}

json ArrangerNode::extraSerialize() {
    json j;
    j["TrackManager"] = sl->tracks->toJSON();
    j["ElementManager"] = sl->em->toJSON();

    json o = json::array();
    for (auto c : outputs.connections) {
        json jc;
        jc["id"] = c->id;
        jc["type"] = c->type;
        o.push_back(jc);
    }
    j["outputs"] = o;

    return j;
}

void ArrangerNode::extraDeSerialize(json j) {

    json o = j["outputs"];
    for (auto jc : o) {
        auto c = new Connection;
        c->id = jc["id"];
        c->dir = Direction::output;
        c->type = jc["type"];

        if (c->type == DataType::Events) {
            c->events = new std::vector<Event>;
        } else {
            c->buffer = new float[bufferSize];
            c->bufferSize = bufferSize;
        }

        outputs.connections.push_back(c);
        outputs.id_pool.reserveID(c->id);
        outputs.ids[c->id] = outputs.connections.size() - 1;

    }

    sl->tracks->fromJSON(j["TrackManager"]);
    sl->em->fromJSON(j["ElementManager"]);
}
