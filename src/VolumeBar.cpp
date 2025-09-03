#include "VolumeBar.h"

VolumeBar::VolumeBar() {};

void VolumeBar::render(SDL_Renderer* renderer, SDL_FRect* dstRect) {
    SDL_SetRenderDrawColor(renderer, 255, 20, 20, 255);
    float w = dstRect->w / 2;
    float h = dstRect->h * (3 / 4.0f) * amplitude;
    SDL_FRect volRect{
        dstRect->x + (dstRect->w/2) - w/2,
        dstRect->y + (dstRect->h/2) + (h/2) - h,
        w,
        h
    };
    SDL_RenderFillRect(renderer, &volRect);

}
