#include "Playhead.h"
#include "fract.h"


Playhead::Playhead(SDL_FRect* gridRect, SDL_FRect* dstRect, bool* detached, fract* startTime) :startTime(startTime) {
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
    float barOffset;
    if (!startTime) {
        barOffset = 0;
    } else {
        barOffset = static_cast<double>(*startTime);
    }
    timePx = (project->tempo * (double)project->effectiveTime / 60 - barOffset) * barWidth  + 1 + gridRect->x;
}
