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

Node::Node(uint16_t id, NodeManager* nm, NodeType nt) :
    id(id),
    project(nm->project),
    nm(nm),
    nodeType(nt),
    isAltPressed(WindowHandler::instance()->isAltPressed),
    isCtrlPressed(WindowHandler::instance()->isCtrlPressed) {
    dstRect = {0, 0, NODE_W, NODE_H};
    w = dstRect.w;
    h = dstRect.h;
    outputs.nodeID = id;
    inputs.nodeID = id;
    outputs.nm = nm;
    inputs.nm = nm;
}

Node::~Node() {
    if (ne) {
        ne->unregisterEmbeddedWindow(this);
        ne->clearPointersToEmbeddedWindow(this);
    }
    if (texture) SDL_DestroyTexture(texture);
    if (vx) delete[] vx;
    if (vy) delete[] vy;
}

connectionSet::~connectionSet() {
    for (auto c : connections) {
        if(c->dir == Direction::output) {
            switch (c->type) {
                case DataType::Events:
                    if (c->events) delete c->events;
                    break;
                case DataType::Waveform:
                    if (c->buffer) delete[] c->buffer;
                    break;
            }
        } // don't delete input connection's data
        delete c;
    }
}

bool Node::handleContentInput(SDL_Event& e) {
    // Ctrl+wheel is editor zoom; forward to inner patcher editors.
    // Return true if consumed by a patcher, false otherwise.
    if (e.type == SDL_EVENT_MOUSE_WHEEL && isCtrlPressed) {
        msX = (*mouseX - dstRect.x) / zoomRatio;
        msY = (*mouseY - dstRect.y) / zoomRatio;
        return handleCustomInput(e);
    }

    // File drops: use drop coordinates if valid, else fall back to mouse position.
    if (e.type == SDL_EVENT_DROP_FILE) {
        float dropX = e.drop.x > 0 ? e.drop.x : *mouseX;
        float dropY = e.drop.y > 0 ? e.drop.y : *mouseY;
        msX = (dropX - dstRect.x) / zoomRatio;
        msY = (dropY - dstRect.y) / zoomRatio;
        if (inPolygon(vx, vy, vCount, msX, msY) || captured_) {
            return handleCustomInput(e);
        }
        return false;
    }

    bool handled = false;

    msX = (*mouseX - dstRect.x) / zoomRatio;
    msY = (*mouseY - dstRect.y) / zoomRatio;

    if (inPolygon(vx, vy, vCount, msX, msY) || captured_) {
        handled = true;
        if (!captured_) hoveredConnection = -1;
    } else if (showConnectionPorts()) {
        bool hoverFound = false;

        const float mx = *mouseX, my = *mouseY;
        for (auto conn : inputs.connections) {
            if (mx >= conn->rect.x && mx <= conn->rect.x + conn->rect.w &&
                my >= conn->rect.y && my <= conn->rect.y + conn->rect.h) {
                hoverFound = true;
                hoveredConnection = conn->id;
                hoveredDirection = Direction::input;
                break;
            }
        }

        if (!hoverFound) {
            for (auto conn : outputs.connections) {
                if (mx >= conn->rect.x && mx <= conn->rect.x + conn->rect.w &&
                    my >= conn->rect.y && my <= conn->rect.y + conn->rect.h) {
                    hoverFound = true;
                    hoveredConnection = conn->id;
                    hoveredDirection = Direction::output;
                    break;
                }
            }
        }

        if (hoverFound) {
            handled = true;
        } else {
            hoveredConnection = -1;
        }
    }

    if (handled) {
        switch (e.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                customInputHandled_ = false;
                if (!handleCustomInput(e))
                    clickMouse(e);
                else
                    customInputHandled_ = true;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                break;
            default:
                break;
        }
        handleWindowInput(e);
    }

    return handled;
}

void Node::clickMouse(SDL_Event& e) {

    if (e.button.button == SDL_BUTTON_LEFT) {
        if (hoveredConnection != -1 && ne) {
            ne->startPortDrag(this, hoveredConnection, hoveredDirection);
        } else if (ne) {
            ne->startNodeDrag(this);
        }

        auto time = SDL_GetTicks();
        auto interval = time - lastLeftClick;
        lastLeftClick = time;

    } else if (e.button.button == SDL_BUTTON_RIGHT) {
        auto* ctxMenu = ContextMenu::get();
        SDL_Window* evWin = SDL_GetWindowFromID(e.button.windowID);
        SDL_Renderer* evRenderer = evWin ? SDL_GetRenderer(evWin) : nullptr;

        Parameter* hoveredParam = nullptr;
        for (auto* p : params) {
            if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) {
                hoveredParam = p;
                break;
            }
        }

        if (hoveredParam) {
            ctxMenu->activate(evRenderer, e.button.windowID);
            size_t paramIndex = 0;
            for (size_t i = 0; i < params.size(); ++i) {
                if (params[i] == hoveredParam) { paramIndex = i; break; }
            }
            auto t = getParameterMenu(hoveredParam, {paramIndex});
            ctxMenu->dynamicTick = getTreeMenuTicker(t);
        } else if (hoveredConnection != -1) {
            if (!ne) {
                ctxMenu->active = false;
                return;
            }
            ctxMenu->activate(evRenderer, e.button.windowID);

            Connection* c;
            switch (hoveredDirection) {
                case Direction::input:
                    c = inputs.getConnection(hoveredConnection);
                    break;
                case Direction::output:
                    c = outputs.getConnection(hoveredConnection);
                    break;
            }

            auto t = getConnectionMenu(c);

            ctxMenu->dynamicTick = getTreeMenuTicker(t);
        } else {
            ctxMenu->activate(evRenderer, e.button.windowID);

            auto t = getNodeMenu();
            ctxMenu->dynamicTick = getTreeMenuTicker(t);
        }
    }
}

void Node::mapParameter(size_t paramIndex) {
    if (!nm || !project || !project->um) return;
    if (paramIndex >= params.size()) return;
    std::vector<int> managerPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new MapParameterUndoAction(project, std::move(managerPath), static_cast<int>(id), paramIndex);
    project->um->newAction(pa);
}

void Node::unmapParameter(size_t paramIndex) {
    if (!nm || !project || !project->um) return;
    if (paramIndex >= params.size()) return;
    Parameter* p = params[paramIndex];
    if (!p || !p->mappedConnection) return;
    std::vector<int> managerPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new UnmapParameterUndoAction(project, std::move(managerPath), static_cast<int>(id), paramIndex);
    project->um->newAction(pa);
}

bool Node::mapParameterNow(size_t paramIndex) {
    if (!nm || paramIndex >= params.size()) return false;
    Parameter* p = params[paramIndex];
    if (!p || p->mappedConnection) return false;

    auto* c = new Connection;
    c->type = DataType::Waveform;
    c->dir = Direction::input;
    inputs.addConnection(c);
    c->label = "Param";
    if (auto* k = dynamic_cast<Knob*>(p)) {
        if (!k->label.empty()) c->label = k->label;
    } else if (!p->label.empty()) {
        c->label = p->label;
    }
    c->label += " (mapped)";

    p->mappedConnection = c;
    makeConnectionRects();
    nm->markTopologyDirty();
    return true;
}

bool Node::unmapParameterNow(size_t paramIndex) {
    if (!nm || paramIndex >= params.size()) return false;
    Parameter* p = params[paramIndex];
    if (!p || !p->mappedConnection) return false;

    Connection* mc = p->mappedConnection;
    if (mc->is_connected)
        nm->severConnectionNow(static_cast<uint16_t>(mc->input_node),
                               static_cast<uint16_t>(mc->input_connection),
                               id, mc->id);

    auto it = std::find(inputs.connections.begin(), inputs.connections.end(), mc);
    if (it != inputs.connections.end()) {
        inputs.id_pool.releaseID(mc->id);
        inputs.ids.erase(mc->id);
        delete mc;
        inputs.connections.erase(it);
    }

    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);

    p->mappedConnection = nullptr;
    makeConnectionRects();
    nm->markTopologyDirty();
    return true;
}

Node* Node::getNodeInput(Connection* con) {
    if (!con->is_connected) return nullptr;
    auto n = nm->getNode(con->input_node);
    return n;
}

uint16_t connectionSet::getIndex(uint16_t id) {
    auto it = ids.find(id);
    if (it == ids.end()) return std::numeric_limits<uint16_t>::max();
    return it->second;
}

Connection* connectionSet::getConnection(uint16_t id) {
    auto index = getIndex(id);
    if (index == std::numeric_limits<uint16_t>::max()) return nullptr;
    if (index >= connections.size()) return nullptr;
    auto con = connections[index];
    return con;
}

void connectionSet::addConnection(Connection* c) {
    c->nm = nm;
    c->id = id_pool.newID();
    connections.push_back(c);
    ids[c->id] = connections.size() -1;

    if (c->dir == Direction::input) {
        c->output_connection = c->id;
        c->output_node = nodeID;
        c->events = nullptr;
        c->buffer = nullptr;
        c->allocChannels = c->numChannels;
    } else {
        if (c->type == DataType::Events) {
            c->events = new std::vector<Event>;
        } else {
            c->allocateBuffer(bufferSize, c->numChannels);
        }
    }
}



void Node::makeConnectionRects() {
    const PortDisplayMode mode = static_cast<PortDisplayMode>(Settings::instance().portDisplayMode());
    float dy = 2;
    float w = (mode == PortDisplayMode::RectLabels) ? 18.0f : 12.0f;
    float h = (mode == PortDisplayMode::RectLabels) ? 64.0f : 12.0f;

    SDL_FRect connRect{
        dstRect.x + dstRect.w / 2 - inputs.connections.size() * w/2, dstRect.y - h - dy,
        w, h
    };

    int inIndex = 0;
    for (auto conn : inputs.connections) {
        conn->rect = connRect;
        conn->displayIndex = inIndex++;
        connRect.x += w;
    }

    connRect.x = dstRect.x + dstRect.w / 2 - outputs.connections.size() * w/2,
    connRect.y = dstRect.y + dstRect.h + dy;

    int outIndex = 0;
    for (auto conn : outputs.connections) {
        conn->rect = connRect;
        conn->displayIndex = outIndex++;
        connRect.x += w;
    }
}

void Node::move(float mx, float my) {
    dstRect.x = mx;
    dstRect.y = my;
    x = mx;
    y = my;
    w = dstRect.w;
    h = dstRect.h;
    makeConnectionRects();
}

void Node::setup() {}

void Node::update(int bufferSize, int sampleRate) {
    this->bufferSize = bufferSize;
    this->sampleRate = sampleRate;

    outputs.bufferSize = bufferSize;
    for(auto c : outputs.connections) {
        if (c->type == DataType::Waveform)
            c->allocateBuffer(bufferSize, c->numChannels);
    }

    inputs.bufferSize = bufferSize;
    setup();
}

void Node::relinkInputs() {
    inputs.bufferSize = bufferSize;
    for (auto c : inputs.connections) {
        if (!c->is_connected) {
            if (c->type == DataType::Waveform) {
                c->buffer = nullptr;
                c->bufferSize = bufferSize;
            } else {
                c->events = nullptr;
            }
            continue;
        }

        auto n = nm->getNode(c->input_node);
        if (!n) {
            c->is_connected = false;
            if (c->type == DataType::Waveform) {
                c->buffer = nullptr;
                c->bufferSize = bufferSize;
            } else {
                c->events = nullptr;
            }
            continue;
        }

        auto ci = n->outputs.getConnection(c->input_connection);
        if (!ci) {
            c->is_connected = false;
            if (c->type == DataType::Waveform) {
                c->buffer = nullptr;
                c->bufferSize = bufferSize;
            } else {
                c->events = nullptr;
            }
            continue;
        }

        if (c->type == DataType::Waveform) {
            c->buffer = ci->buffer;
            c->bufferSize = bufferSize;
        } else {
            c->events = ci->events;
        }
    }
}

bool Node::depends(Node* d) {
    if (d == this) return true;
    for (Connection * c : inputs.connections)
        if (Node* n = getNodeInput(c))
            if (n == d || n->depends(d)) return true;
    return false;
}

void Node::processTree() {
    if (isProcessed) return;
    // process prerequisites
    for (Connection * c : inputs.connections) {
        Node* n = getNodeInput(c);
        if (n) n->processTree();
    }

    // process final
    process();
    isProcessed = true;
}

void Node::resetProcessTree() {
    if (!isProcessed) return;

    for (Connection * c : inputs.connections) {
        Node* n = getNodeInput(c);
        if (n) n->resetProcessTree();
    }

    isProcessed = false;
}

void Node::clearTextures() {
    if (texture) SDL_DestroyTexture(texture);
    texture = nullptr;
    clearParamTextures();
    clearCustomTextures();
}

void Node::attach() {
    x = dstRect.x;
    y = dstRect.y;
    w = dstRect.w;
    h = dstRect.h;
    ne->registerEmbeddedWindow(this);
    clearParamTextures();
    clearCustomTextures();

    mouseX = &(ne->mouseX);
    mouseY = &(ne->mouseY);
}

void Node::handleWindowInput(SDL_Event& e) {
    for (size_t pi = 0; pi < params.size(); ++pi) {
        auto p = params[pi];
        if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) {
            if (p->mappedConnection && p->mappedConnection->is_connected) continue;
            float oldValue = p->value;
            p->handleInput(e);
            if (p->value != oldValue && project && project->um) {
                std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
                // Coalesce rapid wheel events into a single undo action.
                bool merged = false;
                if (project->um->current->type == SetParamValue) {
                    auto* prev = static_cast<SetParamValueUndoAction*>(project->um->current);
                    if (prev->nodeID == static_cast<int>(id) && prev->paramPath == std::vector<size_t>{pi} && prev->managerPath == mgrPath) {
                        prev->newValue = p->value;
                        merged = true;
                        ProjectAction* cap = prev;
                        project->um->enqueueAudioSync([cap]() { cap->doAction(); });
                    }
                }
                if (!merged) {
                    auto* pa = new SetParamValueUndoAction(project, std::move(mgrPath), static_cast<int>(id), {pi},
                        oldValue, p->value, "Knob Change");
                    project->um->newAction(pa);
                }
            }
        }
    }
    if (!customInputHandled_)
        handleCustomInput(e);
    customInputHandled_ = false;
}

void Node::clearParamTextures() {
    for (auto p : params) p->clearTextures();
}

void Node::setNE(NodeEditor* ne) {
    this->ne = ne;
    mouseX = &(ne->mouseX);
    mouseY = &(ne->mouseY);

    attach();

    setNEFinal();
}

void Node::resetNE() {
    if (ne) ne->unregisterEmbeddedWindow(this);
    clearParamTextures();
    clearCustomTextures();
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    ne = nullptr;
    window = nullptr;
    renderer = nullptr;

    resetNEFinal();
}

void Node::buildHitPolygon(std::vector<SDL_FPoint>& out) const {
    if (!vCount || !vx || !vy) {
        EmbeddedWindow::buildHitPolygon(out);
        return;
    }
    out.resize(vCount);
    for (size_t i = 0; i < vCount; ++i)
        out[i] = {dstRect.x + vx[i], dstRect.y + vy[i]};
}
