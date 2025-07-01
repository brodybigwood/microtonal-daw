#include "Playhead.h"


Playhead::Playhead(SDL_FRect* gridRect, SDL_FRect* dstRect, bool* detached) {
    this->project = Project::instance();
    this->detached = detached;
    this->gridRect = gridRect;
    this->dstRect = dstRect;
    
}

Playhead::~Playhead() {

}



void Playhead::setTime(fract time) {
    pos = time;
}

void Playhead::render(SDL_Renderer* renderer, float barWidth, float scrollX) {
    getTimePx(barWidth);
    SDL_SetRenderDrawColor(renderer, colors.playHead[0], colors.playHead[1], colors.playHead[2], colors.playHead[3]);
    SDL_RenderLine(renderer, timePx - scrollX, gridRect->y, timePx - scrollX, gridRect->h + gridRect->y);

}

void Playhead::getTimePx(float barWidth) {
    timePx = project->tempo * (double)project->effectiveTime / 60 * barWidth + 2  + gridRect->x;
}
