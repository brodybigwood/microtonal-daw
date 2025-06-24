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

void Button::setColor(SDL_Color& color) {
    SDL_SetRenderDrawColor(renderer,
                           color.r,
                           color.g,
                           color.b,
                           color.a
    );
}

void Button::toggleState() {
    activated = !activated;
}

void Button::render() {
    if(activated) {
        if(hovered) {
            setColor(active_hover);
        } else {
            setColor(active);
        }
    } else {
        if(hovered) {
            setColor(inactive_hover);
        } else {
            setColor(inactive);
        }
    }
    SDL_RenderFillRect(renderer, &dstRect);

    setColor(border);
    SDL_RenderRect(renderer, &dstRect);
}


Button::~Button() {

}

void Button::hover() {

}
