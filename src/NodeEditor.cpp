#include "NodeEditor.h"
#include "BusManager.h"
#include "NodeManager.h"
#include "Node.h"
#include "ContextMenu.h"
#include <iostream>

void NodeEditor::setMovingNode(Node* node) {
    releaseMovingNode();
    movingNode = node;
    node->moving = true;
    moveOffX = mouseX - node->dstRect.x;
    moveOffY = mouseY - node->dstRect.y;
}

void NodeEditor::releaseMovingNode() {
    if (!movingNode) return;
    movingNode->moving = false;
    movingNode = nullptr;
}

void NodeEditor::setDstConn(Node* node, int id) {
    dstNodeID = id;
    dstNode = node;

    makeConnection();
}

void NodeEditor::setSrcConn(Node* node, int id) {
    srcNodeID = id;
    srcNode = node;

    makeConnection();
}

void NodeEditor::makeConnection() {
    // srcNodeID & dstNodeID are the connection/io ids, not node identifiers
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
    } else if (srcBus != nullptr && dstNode != nullptr
            && dstNodeID != -1) { // bus does not need connection id (only has one output)
        NodeManager* nm = NodeManager::get();
        nm->makeBusConnection(srcBus, dstNode, dstNodeID);
        srcBus = nullptr;
        dstNode = nullptr;
        dstNodeID = -1;
    }
}

NodeEditor::NodeEditor() {

    window = SDL_CreateWindow("Node Editor",
        windowWidth, windowHeight, 0 
    );

    nodeRect = SDL_FRect{
        leftMargin, topMargin,
        windowWidth-leftMargin, windowHeight-topMargin
    };

    renderer = SDL_CreateRenderer(window, NULL);

    BusManager* bm = BusManager::get();
    
    for (int i = 0; i < bm->busCount; i++) {
        auto bus = bm->getBusW(i);
        auto& rect = bus->dstRect;
        
        rect.w = 15.0f;
        rect.h = topMargin/2.0f;
        rect.x = leftMargin + i*rect.w;
        rect.y = 0.0f;    
    }

    for (int i = 0; i < bm->busCount; i++) {
        auto bus = bm->getBusE(i);
        auto& rect = bus->dstRect;
    
        rect.w = 15.0f;
        rect.h = topMargin/2.0f;
        rect.x = leftMargin + i*rect.w;
        rect.y = rect.h;
    }
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

    for (int i = 0; i < bm->busCount; i++) {
        auto* bus = bm->getBusW(i);
        auto& busRect = bus->dstRect;        
 
        SDL_SetRenderDrawColor(renderer, 50,50,50,255);
        SDL_RenderFillRect(renderer, &busRect);
        SDL_SetRenderDrawColor(renderer, 80,80,80,255);
        SDL_RenderRect(renderer, &busRect);
    }

    for (int i = 0; i < bm->busCount; i++) {
        auto* bus = bm->getBusE(i);
        auto& busRect = bus->dstRect;

        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_RenderFillRect(renderer, &busRect);
        SDL_SetRenderDrawColor(renderer, 80,80,80,255);
        SDL_RenderRect(renderer, &busRect);
    }
}

void NodeEditor::renderConnector(SDL_Renderer* renderer) {

    int x;
    int y;

    if (dstNode != nullptr) {
        if (dstNodeID != -1) {
            auto rect = dstNode->inputs.getConnection(dstNodeID)->rect;
            x = rect.x + rect.w/2.0f;
            y = rect.y + rect.h/2.0f; 
        }
    } else if (srcNode != nullptr) {
        if (srcNodeID != -1) {
            auto rect = srcNode->outputs.getConnection(srcNodeID)->rect;
            x = rect.x + rect.w/2.0f;
            y = rect.y + rect.h/2.0f;        
        }
    } else if (srcBus != nullptr) {
        auto rect = srcBus->dstRect;
        x = rect.x + 0.5 * rect.w;
        y = rect.y + 0.5 * rect.h;
    } else {
        return;
    }

    SDL_RenderLine(renderer, mouseX, mouseY, x, y);
}

void NodeEditor::tick() {

    renderInputs();

    render(renderer, &nodeRect);

    renderConnector(renderer);
}

void NodeEditor::renderPresent() {
    SDL_RenderPresent(renderer);
}

uint32_t NodeEditor::getWindowID() {
    return SDL_GetWindowID(window);
}

void NodeEditor::handleInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_UP:
            releaseMovingNode();
            break;
        default:
            break;
    }
    moveMouse();
    if (NodeManager::get()->outNode.handleInput(e)) return;
    for (auto n : NodeManager::get()->getNodes()) {
        if (n->handleInput(e)) return;
    }
    switch(e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            clickMouse(e);
            break;
        case SDL_EVENT_KEY_DOWN:
            keydown(e);
        default:
            break;
    }
}

void NodeEditor::moveMouse() {
    SDL_GetMouseState(&mouseX, &mouseY);
    
    if (movingNode) movingNode->move(mouseX - moveOffX, mouseY - moveOffY);
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

    for(int i = 0; i < BusManager::get()->busCount; i++) {
        auto* bus = BusManager::get()->getBusE(i);
        auto& busRect = bus->dstRect;

        if( inside(mouseX, mouseY, &busRect) ) {
            hoveredBus = bus;
            found_hovered = true;
            break;
        }
    }

    if(!found_hovered) {

        for(int i = 0; i < BusManager::get()->busCount; i++) {
            auto* bus = BusManager::get()->getBusW(i);
            auto& busRect = bus->dstRect;

            if( inside(mouseX, mouseY, &busRect) ) {
                hoveredBus = bus;
                found_hovered = true;
                break;
            }
        }
    }

    if(!found_hovered) hoveredBus = nullptr;
}

void NodeEditor::clickMouse(SDL_Event& e) {
    if (e.button.button == SDL_BUTTON_LEFT) {
        static uint16_t doubleClickThreshold = 512;
        auto time = SDL_GetTicks();
        auto interval = time - lastLeftClick;
        lastLeftClick = time;
        if(interval < doubleClickThreshold) {
            doubleClick();
            return;
        }
    
        srcBus = hoveredBus;
        makeConnection();
        srcNodeID = -1;
        dstNodeID = -1;
        srcNode = nullptr;
        dstNode = nullptr;

    } else if (e.button.button == SDL_BUTTON_RIGHT) {
        auto* ctxMenu = ContextMenu::get();
        ctxMenu->active = true;

        ctxMenu->window_id = SDL_GetWindowID(window);
        ctxMenu->renderer = renderer;

        ctxMenu->locX = mouseX;
        ctxMenu->locY = mouseY;

        auto t = getClickMenu();

        ctxMenu->dynamicTick = getTreeMenuTicker(t);         
    }
}
void NodeEditor::doubleClick() {
    //createNode();
}

std::shared_ptr<TreeEntry> NodeEditor::getClickMenu() {
    auto t = uTreeEntry();
    t->label = "Menu";

    auto create = uTreeEntry();
    create->label = "Add Node";
    
    // create buttons for all node types

    for (int i = 0; i < NodeType::Count; ++i) {
        auto nodeType = uTreeEntry();
        nodeType->label = NodeTypeStr[i];
        nodeType->click = [this, i]() { createNode(static_cast<NodeType>(i)); };

        create->addChild(nodeType);
    }   
    
    t->addChild(create);

    return t;
}

void NodeEditor::createNode(NodeType t) {
    auto node = NodeManager::get()->addNode(t);
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
