#include "Playhead.h"
#include "Transport.h"

Playhead::Playhead(SDL_FRect* gridRect, SDL_FRect* dstRect, Project* p) : project(p) {
    this->gridRect = gridRect;
    this->dstRect = dstRect;
}

Playhead::~Playhead() {
}

void Playhead::render(SDL_Renderer* renderer, float barWidth, float scrollX) {
    getTimePx(barWidth);
    SDL_SetRenderDrawColor(renderer, colors.playHead[0], colors.playHead[1], colors.playHead[2], colors.playHead[3]);
    float lx = dstRect->x + timePx - scrollX;
    float ly0 = dstRect->y + gridRect->y - Transport::kTimelineHeight;
    float ly1 = dstRect->y + gridRect->y + gridRect->h;
    SDL_RenderLine(renderer, lx, ly0, lx, ly1);
}

void Playhead::getTimePx(float barWidth) {
    timePx = project->effectiveTime.load() * barWidth + 1 + gridRect->x;
}
