#include "SongRoll.h"
#include "GridView.h"
#include "Home.h"
#include <SDL3/SDL_events.h>
#include <X11/Xutil.h>
#include "Region.h"
#include "Transport.h"

SongRoll::SongRoll(SDL_FRect* rect, bool* detached) : GridView(detached, rect, 200, &(Project::instance()->startTime)) {
    this->windowHandler = WindowHandler::instance();
    this->project = Project::instance();
    this->instruments = &(project->instruments);

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    regionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    playHeadTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    divHeight = 50;
    cellWidth = barWidth/4;
    minHeight = 1.0f/20;
    ySize = &divHeight;
    xSize = &cellWidth;

    insts = new InstrumentList(dstRect->y + topMargin, dstRect->x + leftMargin, dstRect->h, this->renderer, project, &divHeight);

    UpdateGrid();
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
    transport->render();
}


SongRoll::~SongRoll() {

}

void SongRoll::handleCustomInput(SDL_Event& e) {
    
    switch (e.type) {

        case SDL_EVENT_MOUSE_MOTION:
            getHoveredRegion();
            insts->moveMouse(mouseX, mouseY - topMargin);
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
    for (auto& regionPtr : project->regions) {
        auto localRegion = regionPtr;
        DAW::Region* region = localRegion.get();
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
    for (auto& regionPtr : project->regions) {
        auto localRegion = regionPtr;
        DAW::Region* region = localRegion.get();
        if(
            mouseX > region->startTime*barWidth + leftMargin &&
            mouseX < (region->length+region->startTime)*barWidth + leftMargin &&
            mouseY > region->index*divHeight + topMargin &&
            mouseY < (region->index+1) * divHeight + topMargin
        ) {
            hoveredElement = region->id;
            return;
        }
    }
    hoveredElement = -1;
}

float SongRoll::getHoveredLine() {
    return (mouseY - topMargin)/divHeight;
}

void SongRoll::createElement() {
    fract start = getHoveredTime();
    int y = getHoveredLine();
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
                auto it = std::find_if(project->regions.begin(), project->regions.end(),
                                       [this](const std::shared_ptr<DAW::Region>& reg) {
                                           return reg->id == hoveredElement;
                                       });

                if (it != project->regions.end()) {
                    std::shared_ptr<DAW::Region> reg = *it;
                    if(reg) {
                        windowHandler->createPianoRoll(reg, &(windowHandler->home->pianoRollRect));
                        //InstrumentMenu::instance()->setViewedElement("region", reg->id);
                    }
                } else if (mouseX > leftMargin && mouseX < gridRect.w && mouseY > topMargin && mouseY < gridRect.h) {
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
    auto& regions = project->regions;
    auto it = std::find_if(regions.begin(), regions.end(),
                           [this](const std::shared_ptr<DAW::Region>& r) {
                               return r && r->id == hoveredElement;
                           });

    if (it != regions.end()) {
        regions.erase(it);
    }
}



float SongRoll::getY(float index) {
    return divHeight * index + topMargin;
}

void SongRoll::UpdateGrid() {
    lines.clear();
    float y = 1.0f;
    for (auto& inst : *instruments) {
        y += inst->index;
        lines.push_back(y);
        std::cout<<y<<std::endl;
    }
}
