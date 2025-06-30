#include "Playhead.h"


Playhead::Playhead(SDL_FRect* rect) {
    this->project = Project::instance();

    this->rect = rect;
    
}

Playhead::~Playhead() {

}



void Playhead::setTime(fract time) {
    pos = time;
}

void Playhead::render(SDL_Renderer* renderer, float barWidth, float scrollX) {

    getTimePx(barWidth);
    SDL_SetRenderDrawColor(renderer, colors.playHead[0], colors.playHead[1], colors.playHead[2], colors.playHead[3]);
    SDL_RenderLine(renderer, timePx + rect->x - scrollX, rect->y, timePx + rect->x - scrollX, rect->y + rect->h);

}

void Playhead::getTimePx(float barWidth) {
    
    timePx = project->tempo * (double)project->effectiveTime / 60 * barWidth + 2;

}
