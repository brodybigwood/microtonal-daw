#include "UndoManager.h"
#include "nodes/parametriceq/parametriceq.h"
#include "nodes/vst/vstnode.h"
#include "nodes/arranger/arranger.h"
#include "SongRoll.h"
#include "PianoRollWindow.h"
#include "GridElement.h"
#include <cmath>
#include "SDL_Events.h"
#include "styles.h"
#include <functional>
#include "Project.h"
#include "NodeProcessor.h"
#include "NodeEditor.h"
#include "nodes/nodetypes.h"
#include "NodeManager.h"
#include "InputNode.h"
#include "OutputNode.h"
#include "Note.h"
#include "PianoRoll.h"
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "UndoInternal.h"

// View actions: embedded-window move/resize, visibility, pan/zoom, piano-roll toggle (split from UndoManager.cpp).

MoveEmbeddedWindowAction::MoveEmbeddedWindowAction(Project* p, std::vector<int> managerPath, int ewID, float fromX, float fromY, float toX, float toY) :
        ProjectAction(p, MoveEmbeddedWindow),
        managerPath(std::move(managerPath)),
        ewID(ewID),
        fromX(fromX), fromY(fromY),
        toX(toX), toY(toY) {
    name = "Move Embedded Window";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (!nm.ne) return; // audio thread: GUI-only action
        EmbeddedWindow* ew = nm.ne->getEmbeddedWindowById(this->ewID);
        ew->moveTo(this->toX, this->toY);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (!nm.ne) return;
        EmbeddedWindow* ew = nm.ne->getEmbeddedWindowById(this->ewID);
        ew->moveTo(this->fromX, this->fromY);
    };
}

ResizeEmbeddedWindowAction::ResizeEmbeddedWindowAction(Project* p, std::vector<int> managerPath, int ewID,
        float fromX, float fromY, float fromW, float fromH,
        float toX, float toY, float toW, float toH) :
        ProjectAction(p, ResizeEmbeddedWindow),
        managerPath(std::move(managerPath)),
        ewID(ewID),
        fromX(fromX), fromY(fromY), fromW(fromW), fromH(fromH),
        toX(toX), toY(toY), toW(toW), toH(toH) {
    name = "Resize Embedded Window";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (!nm.ne) return; // audio thread: GUI-only action
        EmbeddedWindow* ew = nm.ne->getEmbeddedWindowById(this->ewID);
        ew->applyGeometry(this->toX, this->toY, this->toW, this->toH);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (!nm.ne) return;
        EmbeddedWindow* ew = nm.ne->getEmbeddedWindowById(this->ewID);
        ew->applyGeometry(this->fromX, this->fromY, this->fromW, this->fromH);
    };
}

ToggleNodeVisibleAction::ToggleNodeVisibleAction(Project* p, std::vector<int> managerPath, int nodeId) :
        ProjectAction(p, ToggleNodeVisible),
        managerPath(std::move(managerPath)),
        nodeId(nodeId) {
    name = "Toggle Node Visible";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* n = nm.getNode(static_cast<uint16_t>(this->nodeId));
        n->visible = !n->visible;
    };
    undoAction = doAction;
}

PanNodesAction::PanNodesAction(Project* p, std::vector<int> managerPath, float dx, float dy) :
        ProjectAction(p, PanNodes),
        managerPath(std::move(managerPath)),
        dx(dx), dy(dy) {
    name = "Pan View";
    skipInitialDo = true;
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        for (auto n : nm.getNodes()) n->move(n->dstRect.x + this->dx, n->dstRect.y + this->dy);
        nm.inNode->move(nm.inNode->dstRect.x + this->dx, nm.inNode->dstRect.y + this->dy);
        nm.outNode->move(nm.outNode->dstRect.x + this->dx, nm.outNode->dstRect.y + this->dy);
        if (nm.ne) { nm.ne->panOffsetX_ += this->dx; nm.ne->panOffsetY_ += this->dy; }
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        for (auto n : nm.getNodes()) n->move(n->dstRect.x - this->dx, n->dstRect.y - this->dy);
        nm.inNode->move(nm.inNode->dstRect.x - this->dx, nm.inNode->dstRect.y - this->dy);
        nm.outNode->move(nm.outNode->dstRect.x - this->dx, nm.outNode->dstRect.y - this->dy);
        if (nm.ne) { nm.ne->panOffsetX_ -= this->dx; nm.ne->panOffsetY_ -= this->dy; }
    };
}

ZoomNodesAction::ZoomNodesAction(Project* p, std::vector<int> managerPath, float amount, float mx, float my) :
        ProjectAction(p, ZoomNodes),
        managerPath(std::move(managerPath)) {
    name = "Zoom View";
    skipInitialDo = true;
    amounts.push_back(amount);
    mxs.push_back(mx);
    mys.push_back(my);
    // Zoom removed — actions are no-ops for backward compat with serialized history.
    doAction = [](){};
    undoAction = [](){};
}

void ZoomNodesAction::addStep(float amount, float mx, float my) {
    amounts.push_back(amount);
    mxs.push_back(mx);
    mys.push_back(my);
    if (propagateCoalesced)
        propagateCoalesced(amount, mx, my);
}

static void openPianoRollWindow(Project* p, const std::vector<int>& managerPath, int arrangerNodeID, int regionID,
                                int ewID, float x, float y, float w, float h, int zOrder) {
    NodeManager& nm = requireManager(p, managerPath);
    if (!nm.ne) return; // audio thread: GUI-only action
    ArrangerNode* arr = undoResolveArrangerNode(p, managerPath, arrangerNodeID);
    if (!arr) return;
    arr->ensureSongRoll();
    if (!arr->sl) return;
    ElementManager* em = arr->elements;
    if (!em) return;
    auto* region = dynamic_cast<Region*>(em->getElement(static_cast<uint16_t>(regionID)));
    if (!region) return;
    arr->sl->createPianoRoll(region, false, ewID);
    if (!arr->sl->pianoRollWindows.empty()) {
        auto* prw = arr->sl->pianoRollWindows.back();
        if (prw) {
            prw->setPosition(static_cast<int>(x), static_cast<int>(y));
            prw->setSize(static_cast<int>(w), static_cast<int>(h));
        }
    }
}

static void closePianoRollWindow(Project* p, const std::vector<int>& managerPath, int arrangerNodeID, int regionID) {
    ArrangerNode* arr = undoResolveArrangerNode(p, managerPath, arrangerNodeID);
    if (!arr || !arr->sl) return;
    arr->sl->clearPianoRoll(regionID, false);
}

TogglePianoRollWindowAction::TogglePianoRollWindowAction(Project* p, std::vector<int> managerPath, int arrangerNodeID, int regionID,
                                                         int ewID, float x, float y, float w, float h, int zOrder, bool open) :
        ProjectAction(p, TogglePianoRollWindow),
        managerPath(std::move(managerPath)),
        arrangerNodeID(arrangerNodeID),
        regionID(regionID),
        ewID(ewID),
        x(x), y(y), w(w), h(h),
        zOrder(zOrder),
        open(open) {
    skipInitialDo = true;
    name = open ? "Open Piano Roll" : "Close Piano Roll";
    doAction = [this]() {
        if (this->open)
            openPianoRollWindow(this->p, this->managerPath, this->arrangerNodeID, this->regionID,
                                this->ewID, this->x, this->y, this->w, this->h, this->zOrder);
        else
            closePianoRollWindow(this->p, this->managerPath, this->arrangerNodeID, this->regionID);
    };
    undoAction = [this]() {
        if (this->open)
            closePianoRollWindow(this->p, this->managerPath, this->arrangerNodeID, this->regionID);
        else
            openPianoRollWindow(this->p, this->managerPath, this->arrangerNodeID, this->regionID,
                                this->ewID, this->x, this->y, this->w, this->h, this->zOrder);
    };
}
