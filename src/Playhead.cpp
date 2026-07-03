#include "Playhead.h"
#include "fract.h"


Playhead::Playhead(SDL_FRect* gridRect, SDL_FRect* dstRect, fract* startTime, Project* p) : startTime(startTime), project(p) {
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
    float lx = dstRect->x + timePx - scrollX;
    float ly0 = dstRect->y + gridRect->y;
    float ly1 = dstRect->y + gridRect->y + gridRect->h;
    SDL_RenderLine(renderer, lx, ly0, lx, ly1);
}

void Playhead::getTimePx(float barWidth) {
    float barOffset;
    if (!startTime) {
        barOffset = 0;
    } else {
        barOffset = static_cast<double>(*startTime);
    }
    timePx = (project->effectiveTime.load() - barOffset) * barWidth + 1 + gridRect->x;
}
