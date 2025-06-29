#include "Playhead.h"


Playhead::Playhead(Project* project, SDL_FRect* rect) {
    this->project = project;

    this->x = &(rect->x);
    this->y = &(rect->y);
    this->w = &(rect->w);
    this->h = &(rect->h);
    
}

Playhead::~Playhead() {

}



void Playhead::setTime(fract time) {
    pos = time;
}

void Playhead::render(SDL_Renderer* renderer, float barWidth) {

    getTimePx(barWidth);
    SDL_SetRenderDrawColor(renderer, colors.playHead[0], colors.playHead[1], colors.playHead[2], colors.playHead[3]);
    SDL_RenderLine(renderer, timePx + *x, *y, timePx + *x, *y + *h);

}

void Playhead::getTimePx(float barWidth) {
    
    timePx = project->tempo * (double)project->effectiveTime / 60 * barWidth + 2;

}
