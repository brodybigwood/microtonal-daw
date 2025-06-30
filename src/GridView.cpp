#include "GridView.h"
#include "Playhead.h"

GridView::GridView() {

}

GridView::~GridView() {
    delete playHead;
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
}

void GridView::handleInput(SDL_Event& e) {
    toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
    toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
    toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);



    handleCustomInput(e);
}

void GridView::setRenderColor(SDL_Renderer* theRenderer, uint8_t code[4]) {
    SDL_SetRenderDrawColor(theRenderer,
                           code[0],
                           code[1],
                           code[2],
                           code[3]
    );
}

void GridView::toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar) {

    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        if (e.key.scancode == keycode) {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                keyVar = true;
            } else if (e.type == SDL_EVENT_KEY_UP) {
                keyVar = false;
            }
        }
    }
}

fract GridView::getHoveredTime() {
    return fract(std::floor((mouseX+scrollX)/cellWidth),notesPerBar);
}
