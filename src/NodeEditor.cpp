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

    NodeManager::get()->render(renderer, &nodeRect);

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
}

void NodeEditor::moveMouse() {
    SDL_GetMouseState(&mouseX, &mouseY);
}

void NodeEditor::clickMouse(SDL_Event& e) {
    static uint16_t doubleClickThreshold = 512;
    auto time = SDL_GetTicks();
    if(time - lastLeftClick < doubleClickThreshold) {
        doubleClick();
    }
    lastLeftClick = time;
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
