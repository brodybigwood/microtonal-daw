#include "NodeEditor.h"
#include "BusManager.h"
#include "NodeManager.h"
#include "Node.h"

NodeEditor::NodeEditor() {

    window = SDL_CreateWindow("Node Editor",
        windowWidth, windowHeight, 0 
    );

    nodeRect = SDL_FRect{
        leftMargin, topMargin,
        windowWidth-leftMargin, windowHeight-topMargin
    };

    renderer = SDL_CreateRenderer(window, NULL);
}

NodeEditor::~NodeEditor() {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

NodeEditor* NodeEditor::get() {
    static NodeEditor n;
    return &n;
}

void NodeEditor::renderInputs() {

    BusManager* bm = BusManager::get();
    
    float w = 15;
    float h = topMargin/2.0f;
    float x = leftMargin;

    size_t i = 0;

    while(x < windowWidth && i < bm->getBusCountW()) {
        SDL_FRect busRect{
            x, 0, w, h
        };

        SDL_SetRenderDrawColor(renderer, 50,50,50,255);
        SDL_RenderFillRect(renderer, &busRect);
        SDL_SetRenderDrawColor(renderer, 80,80,80,255);
        SDL_RenderRect(renderer, &busRect);

        x += w;
    }

    x = leftMargin;

    while(x < windowWidth && i < bm->getBusCountE()) {
        SDL_FRect busRect{
            x, h, w, h 
        };

        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);

        SDL_RenderFillRect(renderer, &busRect);
        SDL_SetRenderDrawColor(renderer, 80,80,80,255);

        SDL_RenderRect(renderer, &busRect);

        x += w;
    }
}

void NodeEditor::tick() {

    renderInputs();

    render(renderer, &nodeRect);

    SDL_RenderPresent(renderer);

}

uint32_t NodeEditor::getWindowID() {
    return SDL_GetWindowID(window);
}

void NodeEditor::handleInput(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            moveMouse();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            clickMouse(e);
            break;
        case SDL_EVENT_KEY_DOWN:
            keydown(e);
        default:
            break;
    }
    if(hoveredNode != nullptr) hoveredNode->handleInput(e);
}

void NodeEditor::moveMouse() {
    SDL_GetMouseState(&mouseX, &mouseY);
    hover();
}

bool NodeEditor::inside(float& mouseX, float& mouseY, SDL_FRect* rect) {
    return (
        mouseX > rect->x &&
        mouseX < rect->x + rect->w &&
        mouseY > rect->y &&
        mouseY < rect->y + rect->h
    );
}

void NodeEditor::hover() {

    bool found_hovered = false;

    for(auto node : NodeManager::get()->getNodes()) {
        SDL_FRect* rect = &(node->dstRect);
        if( inside(mouseX, mouseY, rect) ) {
            hoveredNode = node;
            found_hovered = true;
            break;
        }
    }

    if(inside(mouseX, mouseY, &(NodeManager::get()->outNode.dstRect))) {
        hoveredNode = &(NodeManager::get()->outNode);
        found_hovered = true;
    }

    if(!found_hovered) hoveredNode = nullptr;
    found_hovered = false;

    SDL_FRect busRect{leftMargin, topMargin, 15, topMargin/2.0f};

    for(int i = 0; i < BusManager::get()->getBusCountE(); i++) {
        if( inside(mouseX, mouseY, &busRect) ) {
            hoveredBus = BusManager::get()->getBusE(i);
            found_hovered = true;
            break;
        }
        busRect.x += 15;
    }

    if(!found_hovered) {

        busRect.x = leftMargin;
        busRect.y = topMargin;

        for(int i = 0; i < BusManager::get()->getBusCountW(); i++) {
            if( inside(mouseX, mouseY, &busRect) ) {
                hoveredBus = BusManager::get()->getBusW(i);
                found_hovered = true;
                break;
            }
            busRect.x += 15;
        }
    }

    if(!found_hovered) hoveredBus = nullptr;
}

void NodeEditor::clickMouse(SDL_Event& e) {
    static uint16_t doubleClickThreshold = 512;
    auto time = SDL_GetTicks();
    auto interval = time - lastLeftClick;
    lastLeftClick = time;
    if(interval < doubleClickThreshold) {
        doubleClick();
        return;
    }

    if(hoveredNode != nullptr && hoveredNode->hoveredConnection != -1) {
        switch(hoveredNode->hoveredDirection) {
            case Direction::input:
                dstNode = hoveredNode;
                dstNodeID = hoveredNode->hoveredConnection;
                break;
            case Direction::output:
                srcNode = hoveredNode;
                srcNodeID = hoveredNode->hoveredConnection;
                break;
            default:
                break;
        }
    }
    if(srcNode != nullptr && dstNode != nullptr
            && srcNodeID != -1 && dstNodeID != -1 ) {
        NodeManager* nm = NodeManager::get();
        nm->makeNodeConnection(
                srcNode, srcNodeID,
                dstNode, dstNodeID );

        srcNode = nullptr;
        srcNodeID = -1;
        dstNode = nullptr;
        dstNodeID = -1;
    }
}

void NodeEditor::doubleClick() {
    createNode();
}

void NodeEditor::createNode() {
    auto node = NodeManager::get()->addNode();
    node->move(mouseX, mouseY);
}

void NodeEditor::keydown(SDL_Event& e) {

}

void NodeEditor::render(SDL_Renderer* renderer, SDL_FRect* dstRect) {
    SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
    SDL_RenderFillRect(renderer, dstRect);
    
    auto nm = NodeManager::get();

    for( auto node : nm->getNodes() ) {
        node->render(renderer);
    }

    nm->outNode.render(renderer);
}
