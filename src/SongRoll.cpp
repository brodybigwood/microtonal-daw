#include "SongRoll.h"
#include "GridView.h"
#include "Home.h"
#include <SDL3/SDL_events.h>
#include "Region.h"


SongRoll::SongRoll(SDL_FRect* rect, SDL_Renderer* renderer, bool* detached) : GridView(detached, rect, 200) {
    this->windowHandler = WindowHandler::instance();
    this->project = Project::instance();

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, gridRect.w, gridRect.h);
    regionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, gridRect.w, gridRect.h);
    playHeadTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, gridRect.w, gridRect.h);

    cellHeight = 50;
    cellWidth = 20;



}

bool SongRoll::customTick() {
    RenderGridTexture();
    renderRegions();


    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, colors.background[0], colors.background[1], colors.background[2], colors.background[3]);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer, texture, nullptr, &gridRect);

    SDL_RenderTexture(renderer,gridTexture,nullptr, &gridRect);
    SDL_RenderTexture(renderer,regionTexture,nullptr, &gridRect);

    if(project->processing) {
        playHead->render(renderer, barWidth, scrollX);
    }

    return true;
}


SongRoll::~SongRoll() {

}

void SongRoll::handleCustomInput(SDL_Event& e) {
    
    switch (e.type) {

         case SDL_EVENT_MOUSE_MOTION:
            getHoveredRegion();
        default:
            break;
    }
}

void SongRoll::RenderGridTexture() {
    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 
        colors.grid[0],
        colors.grid[1],
        colors.grid[2],
        colors.grid[3]
    );

    
    for (double x = 0; x < width; x += cellWidth) {
        SDL_RenderLine(renderer, x, 0, x, height);
    }

    
    for (double y = 0; y < height; y += cellHeight) {
        SDL_RenderLine(renderer, 0, y, width, y);
    }


}

void SongRoll::renderRegions() {
    for(size_t i = 0; i<project->regions.size(); i++) {
        renderRegion(i);
    }
}

void SongRoll::renderRegion(int r) {
    if(hoveredElement == r) {
        SDL_SetRenderDrawColor(renderer, 90,90,100,127);
    } else {
        SDL_SetRenderDrawColor(renderer, 20,20,100,127);
    }

    DAW::Region* region = project->regions[r];

    float topLeftCornerX = region->startTime*barWidth;
    float topLeftCornerY = region->y*cellHeight;
    regionRect = {topLeftCornerX, topLeftCornerY, region->length*barWidth, cellHeight};
    SDL_SetRenderTarget(renderer, regionTexture);

    SDL_RenderFillRect(renderer, &regionRect);
}

void SongRoll::getHoveredRegion() {
    for(size_t i = 0; i<project->regions.size(); i++) {
        DAW::Region* region = project->regions[i];
        if(
            mouseX > region->startTime*barWidth + leftMargin &&
            mouseX < (region->length+region->startTime)*barWidth + leftMargin &&
            mouseY > region->y*cellHeight &&
            mouseY < (region->y+1) * cellHeight
        ) {
            hoveredElement = i;
            return;
        } else {
            hoveredElement = -1;
        }
    }
}

void SongRoll::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if(hoveredElement != -1) {
                    windowHandler->createPianoRoll(project->regions[hoveredElement], &(windowHandler->home->pianoRollRect));
                    InstrumentMenu::instance()->setViewedElement("region", hoveredElement);
                    hoveredElement = -1;
                }

            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = false;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = false;
            }
            break;
    }
}

int SongRoll::getHoveredTrack() {
    return (int)mouseY/cellHeight;
}

void SongRoll::createElement() {
    fract time = getHoveredTime();
    int track = getHoveredTrack();
    DAW::Region* reg = new DAW::Region(time, track);
    reg->outputs.push_back(track);
    project->regions.push_back(reg);
}

void SongRoll::deleteElement() {
    if(hoveredElement != -1) {
        project->regions.erase(project->regions.begin() + hoveredElement);

    }
}

void SongRoll::UpdateGrid() {

}
