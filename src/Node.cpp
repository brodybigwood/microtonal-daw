#include "Node.h"
#include "NodeManager.h"
#include "BusManager.h"
#include "NodeEditor.h"

Node::Node(uint16_t id) : id(id) {}

Node::~Node() {}

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

void Node::handleInput(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            SDL_GetMouseState(&mouseX, &mouseY);
            break;
        default:
            break;
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

void Node::move(float& x, float& y) {
    dstRect.x = x;
    dstRect.y = y;
}

SDL_FRect Connection::srcRect() {

    auto s = static_cast<sourceNode*>(data);
    
    SDL_FRect rect{0,0,0,0};

    switch(s->type) {
        case node:
            {
                Node* n = NodeManager::get()->getNode(s->source_id);

                connectionSet& outputs = n->outputs;
                uint16_t index = outputs.getIndex(id);

                rect.w = 10.0f; 
                rect.h = 10.0f;

                rect.x = n->dstRect.x + rect.w * index;
                rect.y = n->dstRect.y + n->dstRect.h - rect.h;

                break;
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

void Node::renderConnection(SDL_Renderer* renderer, uint16_t id) {
    Connection* c = inputs.getConnection(id);
    auto src = c->srcRect();

    uint16_t index = inputs.getIndex(id);

    float x = dstRect.x + 10.0f * index + 5.0f; 
    float y = dstRect.y + 5.0f;
    SDL_RenderLine(renderer, x, y, src.x+src.w/2.0f, src.y+src.h/2.0f);
}

void Node::render(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &dstRect);
    SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    SDL_RenderRect(renderer, &dstRect);

    bool hoverFound = false;

    int numInputs = inputs.connections.size();
    if(numInputs > 0) {
        float w = dstRect.w / numInputs;
        SDL_FRect inputRect{
            dstRect.x, dstRect.y,
            10, 10
        };
        for(auto input: inputs.connections) {
            if( inside(mouseX, mouseY, &inputRect) ) {
                hoveredConnection = input->id;
                hoveredDirection = Direction::input;
                hoverFound = true;
            }
            switch (input->type) {
                case DataType::Events:
                    SDL_SetRenderDrawColor(renderer, 120,255,120,255);
                    break;
                case DataType::Waveform:
                    SDL_SetRenderDrawColor(renderer, 255,120,120,255);
                    break;
            }
            SDL_RenderFillRect(renderer, &inputRect);
            SDL_SetRenderDrawColor(renderer, 120,120,120,255);
            SDL_RenderRect(renderer, &inputRect);
            inputRect.x += w;
        }
    }



    int numOutputs = outputs.connections.size();
    if(numOutputs > 0) {
        float w = dstRect.w / (numOutputs + 1.0f);
        SDL_FRect outputRect{
            dstRect.x, dstRect.y+dstRect.h - 10.0f,
            10, 10
        };
        for(auto output: outputs.connections) {
            if( inside(mouseX, mouseY, &outputRect) ) {
                hoveredConnection = output->id;
                hoveredDirection = Direction::output;
                hoverFound = true;
            }

            switch (output->type) {
                case DataType::Events:
                    SDL_SetRenderDrawColor(renderer, 120,255,120,255);
                    break;
                case DataType::Waveform:
                    SDL_SetRenderDrawColor(renderer, 255,120,120,255);
                    break;
            }
            SDL_RenderFillRect(renderer, &outputRect);
            SDL_SetRenderDrawColor(renderer, 120,120,120,255);
            SDL_RenderRect(renderer, &outputRect);
            outputRect.x += w;
        }
    }

    if(!hoverFound) hoveredConnection = -1;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    for(auto c : inputs.connections) {
        if(c->is_connected) {
            renderConnection(renderer, c->id);
        }
    }
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

void Node::processTree() {

    // process prerequisites
    for (Connection * c : inputs.connections) {
        Node* n = getNodeInput(c);
        if (n) n->processTree();
    }

    // process final
    process();
}
