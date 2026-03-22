#include "NodeEditor.h"
#include "NodeManager.h"
#include "Node.h"
#include "ContextMenu.h"
#include "WindowHandler.h"
#include <iostream>
#include "Preferences.h"

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
        nm->makeNodeConnection(
                srcNode, srcNodeID,
                dstNode, dstNodeID );
        srcNode = nullptr;
        srcNodeID = -1;
        dstNode = nullptr;
        dstNodeID = -1;
    }
}

NodeEditor::NodeEditor() :
    isAltPressed(WindowHandler::instance()->isAltPressed),
    isCtrlPressed(WindowHandler::instance()->isCtrlPressed) {

    window = SDL_CreateWindow("Node Editor",
        windowWidth, windowHeight, 0 
    );

    WindowHandler::instance()->addWindow(this);

    nodeRect = SDL_FRect{
        leftMargin, topMargin,
        windowWidth-leftMargin, windowHeight-topMargin
    };

    renderer = SDL_CreateRenderer(window, NULL);
}

NodeEditor::~NodeEditor() {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    WindowHandler::instance()->removeWindow(this);
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
    } else {
        return;
    }

    SDL_RenderLine(renderer, mouseX, mouseY, x, y);
}

void NodeEditor::tick() {

    render(renderer, &nodeRect); // render background and nodes
    renderConnector(renderer); // connector line from mouse
}

void NodeEditor::renderPresent() {
    for (auto n : nm->getNodes()) {
        n->renderPresent();
    }
    SDL_RenderPresent(renderer);
}

uint32_t NodeEditor::getWindowID() {
    return SDL_GetWindowID(window);
}

void NodeEditor::move() {
    auto x = mouseX - moveOffX;
    auto y = mouseY - moveOffY;

    auto moveNode = [x, y] (Node* n) {
        n->move(n->dstRect.x + x, n->dstRect.y + y);
    };

    for (auto n : nm->getNodes()) moveNode(n);
    moveNode(nm->outNode);

    moveOffX = mouseX;
    moveOffY = mouseY;
}

void NodeEditor::zoom(float amount) {
    for (auto n : nm->getNodes()) if (!n->canZoom(amount)) return;
    if (!nm->outNode->canZoom(amount)) return;
    
    auto zoomNode = [this, amount] (Node* n) {        

        float mx = (n->dstRect.x - mouseX) * amount + mouseX;
        float my = (n->dstRect.y - mouseY) * amount + mouseY;
        
        n->zoom(amount);
        n->move(mx, my);
    };

    for (auto n : nm->getNodes()) zoomNode(n);
    zoomNode(nm->outNode);
}

void NodeEditor::handleInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (isCtrlPressed && leftClick) move();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            leftClick = true;
            moveOffX = mouseX;
            moveOffY = mouseY;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            releaseMovingNode();
            leftClick = false;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (isCtrlPressed) zoom(std::pow(1.1, e.wheel.y));
            break;
        default:
            break;
    }
    moveMouse();
    if (nm->outNode->handleInput(e)) return;
    for (auto n : nm->getNodes()) {
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

}

void NodeEditor::clickMouse(SDL_Event& e) {
    if (e.button.button == SDL_BUTTON_LEFT) {
        auto time = SDL_GetTicks();
        auto interval = time - lastLeftClick;
        lastLeftClick = time;
        if(interval < DCT) {
            doubleClick();
            return;
        }
    
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
    auto node = nm->addNode(t);
    node->move(mouseX, mouseY);
}

void NodeEditor::keydown(SDL_Event& e) {

}

void NodeEditor::render(SDL_Renderer* renderer, SDL_FRect* dstRect) {
    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
    SDL_RenderFillRect(renderer, dstRect);
    
    for( auto node : nm->getNodes() ) {
        node->render();
    }

    nm->outNode->render();
}
