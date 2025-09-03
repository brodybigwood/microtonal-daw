#include "Mixer.h"

Mixer::Mixer() {
    tracks = &(Project::instance()->tracks);
}
Mixer::~Mixer() {}

void Mixer::tick() {
    SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    SDL_RenderFillRect(renderer,dstRect);

    float w = 50;
    SDL_FRect trackRect{
        0,
        dstRect->y,
        w,
        dstRect->h
    };

    for(auto track : *tracks) {
        track->render(renderer,&trackRect);
        trackRect.x += w;
    }
}
