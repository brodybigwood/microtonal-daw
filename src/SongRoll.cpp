#include "SongRoll.h"
#include "GridElement.h"
#include "GridView.h"
#include "Home.h"
#include <SDL3/SDL_events.h>
#include "Region.h"
#include "AudioClip.h"
#include "Transport.h"
#include "ElementManager.h"
#include "SDL_Events.h"

SongRoll::SongRoll(SDL_FRect* rect, bool* detached, Window* w, Project* p, ArrangerNode* n) : GridView(detached, rect, 200, w, p), parentNode(n) {
    this->windowHandler = WindowHandler::instance();

    generateTextures(); 
   
    divHeight = 50;
    minHeight = 1.0f/20;

    UpdateGrid();

    rightMargin = 200;

    rightRect = SDL_FRect{dstRect->x + dstRect->w - rightMargin, dstRect->y + topMargin, rightMargin, dstRect->h - topMargin};

    tracks = new TrackManager;
    tracks->mouseX = &mouseX;
    tracks->mouseY = &mouseY;

    em = new ElementManager(project, tracks, parentNode);
    em->dstRect = &rightRect;

    float x = -1000; //for now only this many measures
    times.clear();
    while(x < 1000) {
        times.push_back(x);
        x++;
    }
    createGridRect();

    tracks->songRoll = this;

    leftRect = SDL_FRect{
        dstRect->x, dstRect->y + topMargin, leftMargin, dstRect->h - topMargin
    };

    tracks->setGeometry(&leftRect, renderer);
    tracks->divHeight = &divHeight;
    tracks->scrollY = &scrollY;
}

bool SongRoll::customTick() {

    auto target = SDL_GetRenderTarget(renderer);

    RenderGridTexture();
    renderElements();


    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, colors.background[0], colors.background[1], colors.background[2], colors.background[3]);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, target);

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
    em->render(renderer);
}


SongRoll::~SongRoll() {

}

void SongRoll::movePosition() {
    if (!movingPosition) return;
    
    auto amnt_x = mouseX - last_lmb_x;
    auto amnt_y = mouseY - last_lmb_x;

    auto oldPos = fract(std::floor((last_lmb_x+scrollX-leftMargin)/(dW / notesPerBar)),notesPerBar);
    auto newPos = fract(std::floor((mouseX+scrollX-leftMargin)/(dW / notesPerBar)),notesPerBar);
    auto change = newPos - oldPos;
    // change = fract(std::floor((amnt_x)/(dW / notesPerBar)),notesPerBar)
    
    movingPosition->start = lastPosition.start + change;

    int trackID = getHoveredTrack(); 
    auto track = tracks->getTrack(trackID);
    auto oldTrack = tracks->getTrack(lastPosition.trackID);
    if (track && oldTrack && track->type == oldTrack->type) movingPosition->trackID = trackID;
}

void SongRoll::handleCustomInput(SDL_Event& e) {
  
    em->mouseX = mouseX;
    em->mouseY = mouseY;
 
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            getHoveredPosition();
            movePosition();
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
        em->handleInput(e);
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
    for (auto element : em->elements) {
        renderElement(element);
    }

    renderDrop(renderer);
}

void SongRoll::renderElement(GridElement* element) {
    float tempo = project->tempo; // notes per minute
    float barsPerSecond = tempo / (60 * notesPerBar);   
    float pixelsPerSecond = dW * barsPerSecond;
    element->draw(renderer, pixelsPerSecond, (int)divHeight);
    SDL_SetRenderTarget(renderer, regionTexture);
    for(auto position : element->positions) {
        auto& pos = *position;
        if(hoveredPosition == &pos) {
            SDL_SetRenderDrawColor(renderer, 90,90,100,127);
        } else {
            SDL_SetRenderDrawColor(renderer, 20,20,100,127);
        }

        float topLeftCornerX = getX(pos.start);
        uint16_t index = tracks->getIndex(pos.trackID);
        float topLeftCornerY = getY(index);
        SDL_FRect dstRectE = {topLeftCornerX, topLeftCornerY, pos.length*dW, divHeight};

        SDL_FRect srcRect;
        switch (element->type) {
            case ElementType::region:
                srcRect = {pos.startOffset * 100, 0, pos.length * 100, 100};
                break;
            case ElementType::audioClip:
                srcRect = {0, 0, dstRectE.w, dstRectE.h};
                break;
            default:
                break;
        }
        SDL_RenderFillRect(renderer, &dstRectE);
        SDL_RenderTexture(renderer, element->texture, &srcRect, &dstRectE);
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderRect(renderer, &dstRectE);
    }
}

void SongRoll::getHoveredPosition() {
    for (auto e : em->elements) {
        for(auto position :e->positions) {
            auto& pos = *position;
            uint16_t index = tracks->getIndex(pos.trackID);
            if(
                mouseX < rightRect.x && 
                mouseX > getX(pos.start) &&
                mouseX < getX(pos.end) &&
                mouseY > getY(index) &&
                mouseY < getY(index+1)
            ) {
                hoveredPosition = &pos;
                return;
            }
        }
    }
    hoveredPosition = nullptr;
}

float SongRoll::getHoveredLine() {
    return (mouseY + scrollY - topMargin)/divHeight;
}

int SongRoll::getHoveredTrack() {
    auto trackIndex = getHoveredLine();
    return tracks->getID(trackIndex);
}

void SongRoll::createElement() {
    int id = em->currentElement;

    if(id == -1) {
        return;
    }
    fract start = getHoveredTime();

    auto trackID = getHoveredTrack();
    auto track = tracks->getTrack(trackID);
    if (!track) return;

    auto elem = em->getElement(id);
    
    if (track->getType() == TrackType::Notes && elem->type != ElementType::region) return;
    if (elem->type == ElementType::region && track->getType() != TrackType::Notes) return;

    elem->createPos(
        start, trackID
    );

    refreshGrid = true;
}

void SongRoll::doubleClick() {
    if (hoveredPosition) {
        auto e = hoveredPosition->element;
        if (e->type == ElementType::region) {
            auto reg = static_cast<Region*>(e);
            windowHandler->createPianoRoll(reg);
        }
    } else {
        auto trackID = getHoveredTrack();
        auto track = tracks->getTrack(trackID);
        if (track && track->type == TrackType::Notes) {
            auto reg = em->newRegion();
            fract start = getHoveredTime();
            reg->createPos(start, trackID);
        }
    }
}

void SongRoll::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:

            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                last_lmb_x = mouseX;
                last_lmb_y = mouseY;

                movingPosition = hoveredPosition;
                if (movingPosition) lastPosition = *movingPosition;
               
                if (SDL_GetTicks() - lastLmbTime < doubleClickTime) doubleClick();
                else if (!hoveredPosition &&
                    mouseX > gridRect.x && mouseX < gridRect.x + gridRect.w &&
                    mouseY > gridRect.y && mouseY < gridRect.y + gridRect.h) {
                    createElement();
                }

                lastLmbTime = SDL_GetTicks();
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                if (!MouseOn(&rightRect) && !MouseOn(&leftRect)) deleteElement();
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = false;
                movingPosition = nullptr;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = false;
            }
            break;
    }
}

void SongRoll::deleteElement() {
    for (auto e : em->elements) {
        auto& positions = e->positions;

        auto it = std::find_if(positions.begin(), positions.end(),
                               [this](const GridElement::Position* p) {
                                   return p == hoveredPosition;
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

void SongRoll::renderDrop(SDL_Renderer* renderer) {
    if (!dropping) return;
    SDL_FRect dropRect{mouseX, mouseY, 150.0f, divHeight};

    SDL_SetRenderDrawColor(renderer, 185, 181, 222, 128);
    SDL_RenderFillRect(renderer, &dropRect);
}

void SongRoll::beginDrop(SDL_DropEvent& d) {
    std::cout << "began drop" << std::endl;
}

void SongRoll::dropFile(SDL_DropEvent& d) {
    std::cout << "DROPPED: " << d.data << std::endl;    
    AudioClip* e = em->newAudioClip(d.data); 
    if (!e) return;

    fract start = getHoveredTime();
    int trackID = getHoveredTrack();

    auto track = tracks->getTrack(trackID);
    if (track  == nullptr) return;
    if (track->type != TrackType::Audio) return; // cant put audioclip on region track

    e->createPos(
        start, trackID
    );

    refreshGrid = true;
}

void SongRoll::clearTextures() {
    SDL_DestroyTexture(texture);
    SDL_DestroyTexture(regionTexture);
    SDL_DestroyTexture(playHeadTexture);
    SDL_DestroyTexture(gridTexture);

    texture = nullptr;
    regionTexture = nullptr;
    playHeadTexture = nullptr;
    gridTexture = nullptr;
}

void SongRoll::generateTextures() {
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    regionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    playHeadTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
}
