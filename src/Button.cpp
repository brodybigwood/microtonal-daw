#include "Button.h"


Button::Button(std::string title, float x, float y, int width = 50, int height = 50, SDL_Renderer* renderer) {

    this->title = title;
    this->width = width;
    this->height = height;
    this->renderer = renderer;

    this->x = x;
    this->y = y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    dstRect = {x, y, width, height};



    
}

void Button::render() {
    hover();
    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
}


Button::~Button() {

}

void Button::hover() {
    if(hovered) {
        color = {20,20,20,255};
    } else {
        color = {255, 255, 255, 255};
    }

}