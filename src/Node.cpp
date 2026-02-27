#include "Node.h"
#include "NodeManager.h"
#include "BusManager.h"
#include "NodeEditor.h"
#include "SDL_Events.h"
#include <iostream>

Node::Node(uint16_t id) : 
    id(id),
    mouseX(NodeEditor::get()->mouseX),
    mouseY(NodeEditor::get()->mouseY) {

}

Node::~Node() {
    SDL_DestroyTexture(texture);
}

connectionSet::~connectionSet() {
    for (auto c : connections) {
        if(c->dir == Direction::output && c->type == DataType::Waveform && c->data) {
            auto ptr = static_cast<WaveformBus*>(c->data);
            delete ptr;
        }

        delete c;
    }
}


void* Node::getOutput(Connection* con) {
    return con->data;
}

bool Node::handleInput(SDL_Event& e) {
    bool handled = false;

    float x = (mouseX - dstRect.x) / zoomRatio;
    float y = (mouseY - dstRect.y) / zoomRatio;

    if (inPolygon(vx, vy, vCount, x, y)) {
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
            default:
                break;
        }
    }

    return handled;
}

void Node::clickMouse(SDL_Event& e) {
    if (e.button.button == SDL_BUTTON_LEFT) {
        auto ne = NodeEditor::get();
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
    }
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
            auto nm = NodeManager::get();
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
            auto nm = NodeManager::get();
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
        zoomRatio = new_w / TEX_W;
    } else {
        zoomRatio = new_h / TEX_H;
    } 

    if (zoomRatio < 0.05) zoomRatio = 0.05;

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
    
    SDL_FRect rect{0,0,0,0};

    switch(s->type) {
        case node:
            {
                Node* n = NodeManager::get()->getNode(s->source_id);

                connectionSet& outputs = n->outputs;
                auto conn = outputs.getConnection(id);

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

                rect = srcBus->dstRect;
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
}

void Node::renderContentHelper(SDL_Renderer* renderer) {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    renderContent(renderer);
    SDL_SetRenderTarget(renderer, target);
    SDL_FRect tRect{0,0,TEX_W,TEX_H};
    SDL_RenderTexture(renderer, texture, &tRect, &dstRect);
}

void Node::render(SDL_Renderer* renderer) {

    if (!texture) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TEX_W, TEX_H);
    }

    renderContentHelper(renderer);

    bool hoverFound = false;

    for(auto conn : inputs.connections) conn->render(renderer, conn->id == hoveredConnection);
    for(auto conn : outputs.connections) conn->render(renderer, conn->id == hoveredConnection);
}

void Connection::render(SDL_Renderer* renderer, bool hover) {
    switch (type) {
        case DataType::Events:
            SDL_SetRenderDrawColor(renderer, 120,255,120,255);
            break;
        case DataType::Waveform:
            SDL_SetRenderDrawColor(renderer, 255,120,120,255);
            break;        
    }

    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 120,120,120,255);
    SDL_RenderRect(renderer, &rect);

    if (!is_connected || dir == Direction::output) return;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    auto src = srcRect();
    SDL_RenderLine(renderer, rect.x+rect.w/2.0f, rect.y+rect.h/2.0f, src.x+src.w/2.0f, src.y+src.h/2.0f);
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

