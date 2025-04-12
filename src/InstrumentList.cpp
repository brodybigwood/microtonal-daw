#include "InstrumentList.h"

InstrumentList::InstrumentList(int y, int width, int height, SDL_Renderer* renderer, Project* project) {
    this->project = project;
    this->y = y;
    this->height = height;
    this->renderer = renderer;
    this->width = width;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    dstRect = {0, 0, width, height};
    noteTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);


}
InstrumentList::~InstrumentList() {

}

void InstrumentList::render() {

    SDL_SetRenderTarget(renderer,noteTexture);
    SDL_SetRenderDrawColor(renderer, 0,0,0,0);
    SDL_RenderClear(renderer);
    for(int i = 0; i<project->instruments.size(); i++) {
        renderInstrument(i);
    }
    SDL_RenderLine(renderer, width, 0, width, height);

    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, 28,28,28,255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer, texture, nullptr, &dstRect);

    SDL_RenderTexture(renderer, noteTexture, nullptr, &dstRect);
}

void InstrumentList::renderInstrument(int i) {

    int xPos = 0;
    int yPos = i*instrumentHeight + y;

    SDL_FRect instPos = {0, yPos, width, instrumentHeight};
    if(i == hoveredInstrument) {
        SDL_SetRenderDrawColor(renderer, 140,140,140,255);
    } else {
        SDL_SetRenderDrawColor(renderer, 40,40,40,255);
    }
    
    SDL_RenderFillRect(renderer, &instPos);



    SDL_SetRenderDrawColor(renderer, 20,20,20,255);
    SDL_RenderLine(renderer, 0, (i+1)*instrumentHeight-1+y, width, (i+1)*instrumentHeight-1+y);

    Instrument instrument = project->instruments[i];

}

void InstrumentList::moveMouse(float x, float y) {
    mouseX = x;
    mouseY = y;
    getHoveredInstrument();
}


void InstrumentList::getHoveredInstrument() {

    for(size_t i = 0; i<project->instruments.size(); i++) {
        Instrument inst = project->instruments[i];
        if(
            mouseX > 0 &&
            mouseX < width &&
            mouseY > instrumentHeight*i &&
            mouseY < instrumentHeight*(i+1)
        ) {
            hoveredInstrument = i;
            return;
        } else {
            hoveredInstrument = -1;
        }
    }

}

void InstrumentList::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if(hoveredInstrument != -1) {
                    hoveredInstrument = -1;
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