#include "SongRoll.h"
#include "GridView.h"
#include "Home.h"
#include <SDL3/SDL_events.h>
#include <X11/Xutil.h>
#include "Region.h"


SongRoll::SongRoll(SDL_FRect* rect, SDL_Renderer* renderer, bool* detached) : GridView(detached, rect, 200, &(Project::instance()->startTime)) {
    this->windowHandler = WindowHandler::instance();
    this->project = Project::instance();
    this->instruments = &(project->instruments);

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
    for(auto region :project->regions) {
        renderRegion(region);
    }
}

void SongRoll::renderRegion(DAW::Region* region) {
    if(hoveredElement == region->id) {
        SDL_SetRenderDrawColor(renderer, 90,90,100,127);
    } else {
        SDL_SetRenderDrawColor(renderer, 20,20,100,127);
    }

    float topLeftCornerX = region->startTime*barWidth + leftMargin;
    float topLeftCornerY = region->index*divHeight + topMargin;
    regionRect = {topLeftCornerX, topLeftCornerY, region->length*barWidth, divHeight};
    SDL_RenderFillRect(renderer, &regionRect);
}

void SongRoll::getHoveredRegion() {
    for(auto region : project->regions) {
        if(
            mouseX > region->startTime*barWidth + leftMargin &&
            mouseX < (region->length+region->startTime)*barWidth + leftMargin &&
            mouseY > region->index*divHeight &&
            mouseY < (region->index+1) * divHeight
        ) {
            hoveredElement = region->id;
            return;
        }
    }
    hoveredElement = -1;
}

float SongRoll::getY() {
    return mouseY/divHeight;
}

void SongRoll::createElement() {
    fract start = getHoveredTime();
    int y = getY();
    if(y >= instruments->size()) {
        return;
    }

    Instrument* inst = (*instruments)[y];
    project->createRegion(start, inst);
    refreshGrid = true;
}

void SongRoll::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:

            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if (hoveredElement >= 0 && hoveredElement < static_cast<int>(project->regions.size())) {
                    DAW::Region* reg = project->regions[hoveredElement];
                    if(reg) {
                        windowHandler->createPianoRoll(reg, &(windowHandler->home->pianoRollRect));
                        InstrumentMenu::instance()->setViewedElement("region", reg->id);
                    }
                } else if (mouseX > leftMargin && mouseX < gridRect.w && mouseY > topMargin && mouseY < gridRect.h) {
                    std::cout<<mouseX<<" "<<mouseY<<std::endl;
                    createElement();
                }


            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                deleteElement();
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

void SongRoll::deleteElement() {
    if (hoveredElement >= 0 && hoveredElement < static_cast<int>(project->regions.size())) {
        DAW::Region* reg = project->regions[hoveredElement];
        if(reg) {
            project->regions.erase(project->regions.begin() + reg->index);
            delete reg;
        }
    }
}

void SongRoll::UpdateGrid() {

}
