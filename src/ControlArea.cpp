#include "ControlArea.h"

ControlArea::ControlArea(int height, int width, SDL_Renderer* renderer) {

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    this->renderer = renderer;

    this->height = height;
    this->width = width;

    dstRect = {x, y, width, height};

}
ControlArea::~ControlArea() {

}


void ControlArea::render() {
    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, 150,150,150,255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
}