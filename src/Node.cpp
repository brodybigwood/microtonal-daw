#include "Node.h"
#include "NodeManager.h"
#include "BusManager.h"
#include "SDL_Events.h"
#include "ContextMenu.h"
#include <iostream>
#include "NodeEditor.h"
#include "WindowHandler.h"

json Node::serialize() {
    json j;

    j["id"] = id;
    j["name"] = name;
    j["zoomRatio"] = zoomRatio;
    j["nodeType"] = nodeType;
    j["x"] = dstRect.x;
    j["y"] = dstRect.y;

    return j;
}

Node* Node::deSerialize(json j, NodeManager* nm) {
    Node* n = nullptr;
    int id = j["id"];

    switch (j["nodeType"].get<int>()) {
        case NodeType::Arranger:
            n = new ArrangerNode(id, nm);
            break;
        case NodeType::Oscillator:
            n = new OscillatorNode(id, nm);
            break;
        case NodeType::Merger:
            n = new MergerNode(id, nm);
            break;
        case NodeType::Splitter:
            n = new SplitterNode(id, nm);
            break;
        case NodeType::Delay:
            n = new DelayNode(id, nm);
            break;
        case NodeType::Panner:
            n = new PannerNode(id, nm);
            break;
    }

    n->name = j["name"];

    n->zoom(j["zoomRatio"].get<float>()/n->zoomRatio);

    n->move(j["x"], j["y"]);

    return n;
}

Node::Node(uint16_t id, NodeManager* nm, NodeType nt) : 
    id(id),
    project(nm->project),
    nm(nm),
    ne(nm->ne),
    nodeType(nt),
    mouseX(nm->ne->mouseX),
    mouseY(nm->ne->mouseY),
    isAltPressed(nm->ne->isAltPressed),
    isCtrlPressed(nm->ne->isCtrlPressed) {
    outputs.nodeID = id;
    inputs.nodeID = id;
    outputs.nm = nm;
    inputs.nm = nm;

    attach();
}

Node::~Node() {
    if (texture) SDL_DestroyTexture(texture);
    if (vx) delete[] vx;
    if (vy) delete[] vy;
}

connectionSet::~connectionSet() {
    for (auto c : connections) {
        if(c->dir == Direction::output) {
            if (c->data) {
                switch (c->type) {
                    case DataType::Events:
                        // havent implemented anything yet
                        break;
                    case DataType::Waveform:
                        auto b = Node::getWaveform(c->data);
                        if (b->buffer) delete[] b->buffer;
                        delete b;
                        break;
                }
            }
        } else if (c->is_connected) if (c->data) delete static_cast<sourceNode*>(c->data);
        delete c;
    }
}


void* Node::getOutput(Connection* con) {
    return con->data;
}

bool Node::handleInput(SDL_Event& e) {
    bool handled = false;

    msX = (mouseX - dstRect.x) / zoomRatio;
    msY = (mouseY - dstRect.y) / zoomRatio;

    if (inPolygon(vx, vy, vCount, msX, msY)) {
        handled = true;
    } else {
        bool hoverFound = false;

        // check if the mouse is hovering over any of the connectors           

        for (auto conn : inputs.connections) {
            if (MouseOn(&conn->rect)) {
                hoverFound = true;
                hoveredConnection = conn->id;
                hoveredDirection = Direction::input;
                break;
            }
        }

        if (!hoverFound) {
            for (auto conn : outputs.connections) {
                if (MouseOn(&conn->rect)) {
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
                clickMouse(e);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (isAltPressed) {
                    zoom(std::pow(1.1, e.wheel.y));
                }
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
        ne->setMovingNode(this);
        if (hoveredConnection != -1) {
            switch (hoveredDirection) {
                case Direction::input:
                    ne->setDstConn(this, hoveredConnection);
                    break;
                case Direction::output:
                    ne->setSrcConn(this, hoveredConnection);
                    break;
            }    
        }

        static uint16_t doubleClickThreshold = 512;
        auto time = SDL_GetTicks();
        auto interval = time - lastLeftClick;
        lastLeftClick = time;
        if(interval < doubleClickThreshold) {
            if (detached) attach();
            else detach();
            return;
        }
    } else if (e.button.button == SDL_BUTTON_RIGHT) {
        auto* ctxMenu = ContextMenu::get();
        ctxMenu->active = true;
        ctxMenu->window_id = SDL_GetWindowID(ne->getWindow());
        ctxMenu->renderer = ne->getRenderer();

        if (hoveredConnection != -1) {
            ctxMenu->locX = mouseX;
            ctxMenu->locY = mouseY;
    
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
            ctxMenu->locX = mouseX;
            ctxMenu->locY = mouseY;
        
            auto t = getNodeMenu();
            ctxMenu->dynamicTick = getTreeMenuTicker(t);
        }
    }
}

std::shared_ptr<TreeEntry> Node::getNodeMenu() {
    auto t = uTreeEntry();
    t->label = "Node Menu";

    if (this->id) { // if there is no id then this is the output node which should not be deleted
        auto remove = uTreeEntry();
        remove->label = "Remove Node";
        remove->click = [this] () { nm->removeNode(this); };
        t->addChild(remove);
    }

    return t;
}

std::shared_ptr<TreeEntry> Node::getConnectionMenu(Connection* c) {
    auto t = uTreeEntry();
    t->label = "Connection Menu";

    auto sever = uTreeEntry();
    sever->label = "Sever Connection";
    sever->click = [c, this]() {this->nm->severConnection(c); };

    t->addChild(sever);

    return t;
}

bool inside(float& mouseX, float& mouseY, SDL_FRect* rect) {
    return (
        mouseX > rect->x &&
        mouseX < rect->x + rect->w &&
        mouseY > rect->y &&
        mouseY < rect->y + rect->h
    );
}

void* Node::getInput(Connection* con) {
    void* data = con->data;
    sourceNode* src = static_cast<sourceNode*>(data);

    switch(src->type) {
        case ConnectionType::bus:
        {
            auto bm = BusManager::get();
            Bus* b;
            switch(con->type) {
                case DataType::Events:
                {                            
                    b = bm->getBusE(src->source_id);
                    break;
                }
                case DataType::Waveform:
                {
                    b = bm->getBusW(src->source_id);
                    break;
                }
            }
            return b;
        }
        case ConnectionType::node:
        {
            auto n = nm->getNode(src->source_id);
            auto oc = n->outputs.getConnection(src->output_id);
            return n->getOutput(oc);
        }
    }
}

Node* Node::getNodeInput(Connection* con) {

    if (!con->is_connected) return nullptr;

    void* data = con->data;
    sourceNode* src = static_cast<sourceNode*>(data);

    switch(src->type) {
        case ConnectionType::bus:
            return nullptr;
        case ConnectionType::node:
        {
            auto n = nm->getNode(src->source_id);
            return n;
        }
    }
}

uint16_t connectionSet::getIndex(uint16_t id) {
    return ids[id];
}

Connection* connectionSet::getConnection(uint16_t id) {
    auto index = getIndex(id);
    auto con = connections[index];
    return con;
}

void connectionSet::addConnection(Connection* c) {
    c->nm = nm;
    if (c->dir == Direction::input) c->output_node = nodeID;
    c->id = id_pool.newID();
    connections.push_back(c);
    ids[c->id] = connections.size() -1;
    if(c->dir == Direction::input) return;
    switch(c->type) {
        case DataType::Waveform:
            c->data = new WaveformBus;
            break;
        default:
            break;
    }
            
}

void Node::resize(float rx, float ry) {

    float new_w = dstRect.w + rx;
    float new_h = dstRect.h + ry;

    if (new_w > new_h) {
        zoom((new_w / TEX_W)/zoomRatio);
    } else {
        zoom((new_h / TEX_H)/zoomRatio);

    } 

    zoom(1.0f);
}

bool Node::canZoom(float amount) {
    return zoomRatio * amount >= 0.01;
}

void Node::zoom(float amount) {
 
    if (!canZoom(amount)) return;

    zoomRatio *= amount;

    dstRect.w = TEX_W * zoomRatio;
    dstRect.h = TEX_H * zoomRatio;

    float dy = 2;
    float w = 10; 
    float h = 10; 

    SDL_FRect connRect{
        dstRect.x + dstRect.w / 2 - inputs.connections.size() * w/2, dstRect.y - h - dy,
        w, h
    };

    for (auto conn: inputs.connections) {
        conn->rect = connRect;
        connRect.x += w;
    }

    connRect.x = dstRect.x + dstRect.w / 2 - outputs.connections.size() * w/2,
    connRect.y = dstRect.y + dstRect.h + dy;

    for (auto conn : outputs.connections) {
        conn->rect = connRect;
        connRect.x += w;
    }
}

void Node::move(float x, float y) {
    dstRect.x = x;
    dstRect.y = y;
    resize(0, 0);
}

SDL_FRect Connection::srcRect() {

    auto s = static_cast<sourceNode*>(data);
    
    switch(s->type) {
        case node:
            {
                Node* n = nm->getNode(s->source_id);

                connectionSet& outputs = n->outputs;
                auto conn = outputs.getConnection(s->output_id);

                return conn->rect;
            }
        case bus: {
                auto bm = BusManager::get();
                uint16_t& index = s->source_id; // id = index for busses

                Bus* srcBus;
                switch(type) {
                    case DataType::Events: {
                        srcBus = bm->getBusE(index);
                        break;
                    }
                    case DataType::Waveform: {
                        srcBus = bm->getBusW(index);
                        break;
                    }
                }

                return srcBus->dstRect;
            }
            break;
        default:
            break;
    }

    return rect;
}

void Node::renderContent(SDL_Renderer* renderer) {
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];

        vx[0] = 0;
        vx[1] = 1920;
        vx[2] = 1620;
        vx[3] = 300;

        vy[0] = 0;
        vy[1] = 0;
        vy[2] = 1080;
        vy[3] = 1080;
    }

    filledPolygonRGBA(renderer, vx, vy, vCount, 255, 255, 255, 255);
    aapolygonRGBA(renderer, vx, vy, vCount, 0, 0, 0, 255);

    renderParams(renderer);
}

void Node::renderContentHelper(SDL_Renderer* renderer) {
    SDL_Texture* tex;
    if (detached) tex = texture_detached;
    else tex = texture;

    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, tex);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    renderContent(renderer);
    SDL_SetRenderTarget(renderer, target);
    SDL_FRect tRect{0,0,TEX_W,TEX_H};
    SDL_RenderTexture(ne->renderer, texture, &tRect, &dstRect);

    if (detached) {
        SDL_RenderTexture(renderer, texture_detached, &tRect, NULL);
    }
}

void Node::render() {

    if (!detached && !texture) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TEX_W, TEX_H);
    } else if (detached && !texture_detached) {
        texture_detached = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TEX_W, TEX_H);
    }

    renderContentHelper(renderer);

    for(auto conn : inputs.connections) conn->render(ne->renderer, conn->id == hoveredConnection);
    for(auto conn : outputs.connections) conn->render(ne->renderer, conn->id == hoveredConnection);
}

void Connection::render(SDL_Renderer* renderer, bool hover) {
    SDL_Color c;

    switch (type) {
        case DataType::Events:
            if (is_connected) c = {160, 255, 160, 255};
            else c = {120, 255, 120, 255};
            break;
        case DataType::Waveform:
            if (is_connected) c = {255, 160, 160, 255};
            else c = {255, 120, 120, 255};
            break;        
    }

    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);

    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 120,120,120,255);
    SDL_RenderRect(renderer, &rect);

    if (!is_connected || dir == Direction::output) return;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    auto src = srcRect();
    SDL_RenderLine(renderer, rect.x+rect.w/2.0f, rect.y+rect.h/2.0f, src.x+src.w/2.0f, src.y+src.h/2.0f);
}

void Node::renderParams(SDL_Renderer* renderer) {
    for (auto p : params) p->render(renderer);
}

void Node::setup() {}

void Node::update(int bufferSize, int sampleRate) {
    this->bufferSize = bufferSize;
    this->sampleRate = sampleRate;
    for(auto c : outputs.connections) {
        if (c->type == DataType::Waveform) {
            auto b = getWaveform(c->data);
            if(b->buffer != nullptr) {
                delete[] b->buffer;
            }
            b->buffer = new float[bufferSize];
            b->bufferSize = bufferSize;
        }
    }
    setup();
}

EventBus* Node::getEvents(void* data){
    return static_cast<EventBus*>(data);
}

WaveformBus* Node::getWaveform(void* data){
    return static_cast<WaveformBus*>(data);
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

void Node::detach() {
    if (!detached) {
        window = SDL_CreateWindow(name.c_str(), TEX_W, TEX_H, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);
        SDL_SetWindowParent(window, ne->window);
        renderer = SDL_CreateRenderer(window, NULL);
        WindowHandler::instance()->addWindow(this);
    }
    detached = true;

    clearParamTextures();
    clearCustomTextures();
}

void Node::attach() {
    if (detached) {
        if (window) SDL_DestroyWindow(window);
        if (renderer) SDL_DestroyRenderer(renderer);
        WindowHandler::instance()->removeWindow(this);
    }
    if (texture_detached) SDL_DestroyTexture(texture_detached);

    window = ne->window;
    renderer = ne->renderer;
    texture_detached = nullptr;
    clearParamTextures();
    clearCustomTextures();

    detached = false;
}

void Node::handleWindowInput(SDL_Event& e) {
    if (detached && SDL_GetWindowFromID(getEventWindowID(e)) == window) {
        SDL_GetMouseState(&msX, &msY);
    }
    for (auto p : params) if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) p->handleInput(e);
    handleCustomInput(e);
}

void Node::clearParamTextures() {
    for (auto p : params) p->clearTextures();
}
