#include "Button.h"


Button::Button(SDL_Renderer* renderer) :renderer(renderer) {
}

void Button::setColor(SDL_Color& color) {
    SDL_SetRenderDrawColor(renderer,
                           color.r,
                           color.g,
                           color.b,
                           color.a
    );
}

void Button::render() {
    if(activated()) {
        if(hover()) {
            setColor(active_hover);
        } else {
            setColor(active);
        }
    } else {
        if(hover()) {
            setColor(inactive_hover);
        } else {
            setColor(inactive);
        }
    }
    SDL_RenderFillRect(renderer, dstRect);

    setColor(border);
    SDL_RenderRect(renderer, dstRect);
}


Button::~Button() {

}
