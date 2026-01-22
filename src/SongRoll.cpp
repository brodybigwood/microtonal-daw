#include "SongRoll.h"
#include "GridElement.h"
#include "GridView.h"
#include "Home.h"
#include <SDL3/SDL_events.h>
#include "Region.h"
#include "Transport.h"
#include "ElementManager.h"

SongRoll::SongRoll(SDL_FRect* rect, bool* detached) : GridView(detached, rect, 200) {
    this->windowHandler = WindowHandler::instance();
    this->project = Project::instance();

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    regionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    playHeadTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    divHeight = 50;
    minHeight = 1.0f/20;

    UpdateGrid();

    rightMargin = 200;

    rightRect = SDL_FRect{dstRect->x + dstRect->w - rightMargin, dstRect->y + topMargin, rightMargin, dstRect->h - topMargin};

    ElementManager::get()->dstRect = &rightRect;



    float x = -1000; //for now only this many measures
    times.clear();
    while(x < 1000) {
        times.push_back(x);
        x++;
    }
    createGridRect();

    tracks = TrackList::get();
    leftRect = SDL_FRect{
        dstRect->x, dstRect->y + topMargin, leftMargin, dstRect->h - topMargin
    };

    tracks->setGeometry(&leftRect);
    tracks->divHeight = &divHeight;
    tracks->scrollY = &scrollY;

    tracks->mouseX = &mouseX;
    tracks->mouseY = &mouseY;
}

bool SongRoll::customTick() {
    RenderGridTexture();
    renderElements();


    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, colors.background[0], colors.background[1], colors.background[2], colors.background[3]);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);

    SDL_RenderTexture(renderer, texture, nullptr, dstRect);
    SDL_RenderTexture(renderer,gridTexture,nullptr, dstRect);
    SDL_RenderTexture(renderer,regionTexture,nullptr, dstRect);

    if(project->processing) {
        playHead->render(renderer, dW, scrollX);
    }

    renderMargins();

    return true;
}

void SongRoll::renderMargins() {
    tracks->render(renderer);
    transport->render();
    ElementManager::get()->render(renderer);
}


SongRoll::~SongRoll() {

}

void SongRoll::handleCustomInput(SDL_Event& e) {
    
    switch (e.type) {

        case SDL_EVENT_MOUSE_MOTION:
            getHoveredRegion();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if(scrollY < 0) {
                scrollY = 0;
            }
            break;
        default:
            break;
    }
    if (mouseX > rightRect.x && mouseX <= (rightRect.x + rightRect.w) &&
        mouseY > rightRect.y && mouseY <= (rightRect.y + rightRect.h)) {
        ElementManager::get()->handleInput(e);
    }

    if (mouseX > leftRect.x && mouseX <= (leftRect.x + leftRect.w) &&
        mouseY > leftRect.y && mouseY <= (leftRect.y + leftRect.h)) {
        tracks->handleInput(e);
    } else tracks->hoveredTrack = nullptr;

}

void SongRoll::renderElements() {
    SDL_SetRenderTarget(renderer, regionTexture);
    SDL_SetRenderDrawColor(renderer, 0,0,0,0);
    SDL_RenderClear(renderer);
    for (auto element : ElementManager::get()->elements) {
        renderElement(element);
    }
}

void SongRoll::renderElement(GridElement* element) {
    element->draw(renderer);
    SDL_SetRenderTarget(renderer, regionTexture);
    for(auto pos : element->positions) {
        if(hoveredElement == pos.id) {
            SDL_SetRenderDrawColor(renderer, 90,90,100,127);
        } else {
            SDL_SetRenderDrawColor(renderer, 20,20,100,127);
        }

        float topLeftCornerX = getX(pos.start);
        uint16_t index = tracks->getIndex(pos.trackID);
        float topLeftCornerY = getY(index);
        SDL_FRect dstRect = {topLeftCornerX, topLeftCornerY, pos.length*dW, divHeight};
        SDL_FRect srcRect = {pos.startOffset * 100, 0, pos.length * 100, 100};
        SDL_RenderFillRect(renderer, &dstRect);
        SDL_RenderTexture(renderer, element->texture, &srcRect, &dstRect);
    }
}

void SongRoll::getHoveredRegion() {
    for (auto e : ElementManager::get()->elements) {
        for(auto pos :e->positions) {
            uint16_t index = tracks->getIndex(pos.trackID);
            if(
                mouseX > getX(pos.start) &&
                mouseX < getX(pos.end) &&
                mouseY > getY(index) &&
                mouseY < getY(index+1)
            ) {
                hoveredElement = pos.id;
                return;
            }
        }
    }
    hoveredElement = -1;
}

float SongRoll::getHoveredLine() {
    return (mouseY + scrollY - topMargin)/divHeight;
}

void SongRoll::createElement() {
    int id = ElementManager::get()->currentElement;

    if(id == -1) {
        return;
    }
    fract start = getHoveredTime();
    int trackIndex = getHoveredLine();

    if (TrackList::get()->getTrack(trackIndex) == nullptr) return; 

    auto elem = ElementManager::get()->getElement(id);

    elem->createPos(
        start, trackIndex
    );

    refreshGrid = true;
}

void SongRoll::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:

            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                for (auto e : ElementManager::get()->elements) {
                    auto& positions = e->positions;
                    auto it = std::find_if(positions.begin(), positions.end(),
                                           [this](const GridElement::Position& pos) {
                                               return pos.id == hoveredElement;
                                           });

                    if (it != positions.end()) {
                        if(e->type == ElementType::region) {
                            auto reg = static_cast<Region*>(e);
                            windowHandler->createPianoRoll(reg, &(windowHandler->home->pianoRollRect));
                            return;
                        }
                    }
                }

                if (mouseX > gridRect.x && mouseX < gridRect.x + gridRect.w &&
                    mouseY > gridRect.y && mouseY < gridRect.y + gridRect.h) {
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
    for (auto e : ElementManager::get()->elements) {
        auto& positions = e->positions;

        auto it = std::find_if(positions.begin(), positions.end(),
                               [this](const GridElement::Position& p) {
                                   return p.id == hoveredElement;
                               });

        if (it != positions.end()) {
            positions.erase(it);
            return;
        }
    }
}




float SongRoll::getY(float index) {
    return divHeight * index + topMargin - scrollY;
}

void SongRoll::UpdateGrid() {
    lines.clear();
    float y = -20;
    while(y<20) {
        lines.push_back(y);
        y++;
    }
}
