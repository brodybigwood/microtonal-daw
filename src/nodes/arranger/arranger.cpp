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

