#include "NodeEditor.h"
#include "Wavnode.h"

NodeEditor::NodeEditor() {

    window = SDL_CreateWindow("Node Editor",
        800, 600, 0 
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

void NodeEditor::tick() {
    Wavnode* node = Wavnode::get();

    SDL_SetRenderDrawColor(renderer, 50, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
}

void handleInput(SDL_Event& e) {

}
