#include "NodeEditor.h"
#include "BusManager.h"

NodeEditor::NodeEditor() {

    window = SDL_CreateWindow("Node Editor",
        windowWidth, windowHeight, 0 
    );

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
    float h = 10;
    float x = 0;

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

    x = 0;

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

    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderClear(renderer);

    renderInputs();

    SDL_RenderPresent(renderer);
}

void handleInput(SDL_Event& e) {

}
