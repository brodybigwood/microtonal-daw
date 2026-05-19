#include "Transport.h"
#include "GridView.h"
#include "Project.h"
#include "Button.h"
#include <functional>

Transport::Transport(GridView* view) : view(view), dstRect(view->dstRect) {
}

void Transport::moveMouse(float mouseX, float mouseY) {
    this->mouseX = mouseX;
    this->mouseY = mouseY;
}

void Transport::clickMouse() {
}

Transport::~Transport() {
}

void Transport::render(SDL_Renderer* renderer) {
    uint8_t color[4] = {50, 50, 50, 255};
    SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], color[3]);
    SDL_FRect rect{view->dstRect->x,view->dstRect->y,view->dstRect->w, view->topMargin};
    SDL_RenderFillRect(renderer, &rect);
}
