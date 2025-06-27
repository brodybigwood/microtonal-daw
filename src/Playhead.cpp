#include "Playhead.h"


Playhead::Playhead(SDL_Renderer* renderer, SDL_Texture* ntexture, Project* project, WindowHandler* windowHandler) {
    this->windowHandler = windowHandler;
    this->project = project;
    this->renderer = renderer;
    this->ntexture = ntexture;


    SDL_GetTextureSize(ntexture, &width, &height);
    
}

Playhead::~Playhead() {

}



void Playhead::setTime(fract time) {
    pos = time;
}

void Playhead::render(float barWidth) {

    getTimePx(barWidth);

    SDL_SetRenderTarget(renderer, ntexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);


    SDL_SetRenderDrawColor(renderer, colors.playHead[0], colors.playHead[1], colors.playHead[2], colors.playHead[3]);
    SDL_RenderLine(renderer, timePx, 0, timePx, height);

}

void Playhead::getTimePx(float barWidth) {
    
    timePx = project->tempo * (double)project->effectiveTime / 60 * barWidth;

}
