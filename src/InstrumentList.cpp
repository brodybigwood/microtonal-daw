#include "InstrumentList.h"

InstrumentList::InstrumentList(int height, SDL_Renderer* renderer) {

    this->height = height;
    this->renderer = renderer;
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    dstRect = {0, 0, width, height};
    noteTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    for(int i = 0; i<10; i++) {
        Instrument* inst = new Instrument();
        instruments.push_back(inst);
    }


}
InstrumentList::~InstrumentList() {

}

void InstrumentList::render() {

    SDL_SetRenderTarget(renderer,noteTexture);
    SDL_SetRenderDrawColor(renderer, 0,0,0,0);
    SDL_RenderClear(renderer);
    for(int i = 0; i<instruments.size(); i++) {
        renderInstrument(i);
    }

    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, 50,50,50,255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer, texture, nullptr, &dstRect);

    SDL_RenderTexture(renderer, noteTexture, nullptr, &dstRect);
}

void InstrumentList::renderInstrument(int i) {

    SDL_FRect instPos = {0, i*instrumentHeight, width, instrumentHeight};

    SDL_SetRenderDrawColor(renderer, 35,35,35,255);
    SDL_RenderFillRect(renderer, &instPos);
    SDL_SetRenderDrawColor(renderer, 0,0,0,255);
    SDL_RenderLine(renderer, 0, (i+1)*instrumentHeight-1, width, (i+1)*instrumentHeight-1);
}