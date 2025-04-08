#include "ControlArea.h"

ControlArea::ControlArea(int x, int y, int width, int height, SDL_Renderer* renderer) {

    this->width = width;
    this->height = height;
    this->renderer = renderer;

    this->x = x;
    this->y = y;
}
ControlArea::~ControlArea() {

}