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

Parameter* Node::resolveParameterPath(const std::vector<size_t>& path) {
    if (path.empty() || path[0] >= params.size()) return nullptr;
    Parameter* p = params[path[0]];
    for (size_t i = 1; i < path.size(); ++i) {
        size_t modIdx = path[i];
        if (modIdx >= p->modulators.size()) return nullptr;
        p = &p->modulators[modIdx]->depth;
    }
    return p;
}

std::string Node::parameterPathLabel(const std::vector<size_t>& path) const {
    if (path.empty()) return "Param";
    std::string label;
    Parameter* p = params[path[0]];
    if (auto* k = dynamic_cast<Knob*>(p)) {
        label = k->label.empty() ? "Param" : k->label;
    } else {
        label = "Param";
    }
    for (size_t i = 1; i < path.size(); ++i) {
        label += ".mod" + std::to_string(path[i]) + ".depth";
    }
    return label;
}

void Node::addModSource(Parameter* p) {
    if (!p) return;
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == p) {
            addModSource({i});
            return;
        }
    }
}

void Node::addModSource(const std::vector<size_t>& path) {
    if (!nm || !project || !project->um) return;
    if (path.empty() || path[0] >= params.size()) return;
    std::vector<int> managerPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new AddModSourceUndoAction(project, std::move(managerPath), static_cast<int>(id), path);
    pa->name = "Add Mod Source";
    project->um->newAction(pa);
}

bool Node::addModSourceNow(const std::vector<size_t>& path) {
    if (!nm) return false;
    Parameter* p = resolveParameterPath(path);
    if (!p) return false;

    bool changed = false;
    {
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.addConnection(c);
        c->label = parameterPathLabel(path);
        p->addModulator(new Modulator(c->buffer, true, generateRect(0, 0, 200, 10), 0.5f, c));
        makeConnectionRects();
        nm->markTopologyDirty();
        changed = true;
    }
    return changed;
}

void Node::removeModSource(Parameter* p, size_t modIndex) {
    if (!p || modIndex >= p->modulators.size()) return;
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == p) {
            removeModSource({i}, modIndex);
            return;
        }
    }
}

void Node::removeModSource(const std::vector<size_t>& path, size_t modIndex) {
    if (!nm || !project || !project->um) return;
    if (path.empty() || path[0] >= params.size()) return;
    std::vector<int> managerPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new RemoveModSourceUndoAction(project, std::move(managerPath), static_cast<int>(id), path, modIndex);
    pa->name = "Remove Mod Source";
    project->um->newAction(pa);
}

bool Node::removeModSourceNow(const std::vector<size_t>& path, size_t modIndex) {
    if (!nm) return false;
    Parameter* p = resolveParameterPath(path);
    if (!p || modIndex >= p->modulators.size()) return false;

    Modulator* m = p->modulators[modIndex];
    if (!m) return false;

    // Sever any wired cables in the modulator tree, then remove.
    std::function<void(Modulator*)> severTree = [&](Modulator* mod) {
        for (auto* nested : mod->depth.modulators) severTree(nested);
        if (mod->sourceConnection && mod->sourceConnection->is_connected) {
            nm->severConnectionNow(
                static_cast<uint16_t>(mod->sourceConnection->output_node),
                static_cast<uint16_t>(mod->sourceConnection->output_connection),
                id, mod->sourceConnection->id);
            mod->sourceConnection->is_connected = false;
        }
    };
    severTree(m);

    // Remove connections from inputs.
    std::function<void(Modulator*)> removeConns = [&](Modulator* mod) {
        for (auto* nested : mod->depth.modulators) removeConns(nested);
        if (mod->sourceConnection) {
            auto it = std::find(inputs.connections.begin(), inputs.connections.end(), mod->sourceConnection);
            if (it != inputs.connections.end()) {
                inputs.id_pool.releaseID(mod->sourceConnection->id);
                inputs.ids.erase(mod->sourceConnection->id);
                delete mod->sourceConnection;
                inputs.connections.erase(it);
            }
        }
    };
    removeConns(m);

    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);
    makeConnectionRects();
    nm->markTopologyDirty();

    delete m;
    p->modulators.erase(p->modulators.begin() + static_cast<ptrdiff_t>(modIndex));
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
            int nch = c->numChannels;
            c->buffer = new float[static_cast<size_t>(bufferSize) * static_cast<size_t>(nch)];
            c->bufferSize = bufferSize;
            c->allocChannels = nch;
            if (bufferSize > 0) {
                std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(nch) * sizeof(float));
            }
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
        if (c->type == DataType::Waveform) {
            if(c->buffer != nullptr) {
                delete[] c->buffer;
            }
            int nch = c->numChannels;
            size_t allocSize = static_cast<size_t>(bufferSize) * static_cast<size_t>(nch);
            c->buffer = new float[allocSize];
            c->bufferSize = bufferSize;
            c->allocChannels = nch;
            if (bufferSize > 0) {
                std::memset(c->buffer, 0, allocSize * sizeof(float));
            }
        }
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



