#include "SongRoll.h"
#include "GridView.h"
#include "Home.h"
#include <SDL3/SDL_events.h>
#include "Region.h"


SongRoll::SongRoll(SDL_FRect* rect, SDL_Renderer* renderer, bool* detached) : GridView(detached, rect, 200) {
    this->windowHandler = WindowHandler::instance();
    this->project = Project::instance();

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    regionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    playHeadTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    divHeight = 50;
    cellWidth = barWidth/4;
    ySize = &divHeight;
    xSize = &cellWidth;

    insts = new InstrumentList(dstRect->y, leftMargin, dstRect->h, this->renderer, project);
}

bool SongRoll::customTick() {
    RenderGridTexture();
    renderRegions();


    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, colors.background[0], colors.background[1], colors.background[2], colors.background[3]);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);

    SDL_RenderTexture(renderer, texture, nullptr, dstRect);
    SDL_RenderTexture(renderer,gridTexture,nullptr, dstRect);
    SDL_RenderTexture(renderer,regionTexture,nullptr, dstRect);

    if(project->processing) {
        playHead->render(renderer, barWidth, scrollX);
    }

    renderMargins();

    return true;
}

void SongRoll::renderMargins() {
    insts->render();
}


SongRoll::~SongRoll() {

}

void SongRoll::handleCustomInput(SDL_Event& e) {
    
    switch (e.type) {

        case SDL_EVENT_MOUSE_MOTION:
            getHoveredRegion();
            insts->moveMouse(mouseX, mouseY);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            insts->clickMouse(e);
        default:
            break;
    }
}

void SongRoll::renderRegions() {
    SDL_SetRenderTarget(renderer, regionTexture);
    SDL_SetRenderDrawColor(renderer, 0,0,0,0);
    SDL_RenderClear(renderer);
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

    float topLeftCornerX = region->startTime*barWidth + leftMargin;
    float topLeftCornerY = region->y*divHeight + topMargin;
    regionRect = {topLeftCornerX, topLeftCornerY, region->length*barWidth, divHeight};
    SDL_RenderFillRect(renderer, &regionRect);
}

void SongRoll::getHoveredRegion() {
    for(size_t i = 0; i<project->regions.size(); i++) {
        DAW::Region* region = project->regions[i];
        if(
            mouseX > region->startTime*barWidth + leftMargin &&
            mouseX < (region->length+region->startTime)*barWidth + leftMargin &&
            mouseY > region->y*divHeight &&
            mouseY < (region->y+1) * divHeight
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
    return (int)mouseY/divHeight;
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
