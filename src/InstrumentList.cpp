#include "InstrumentList.h"

InstrumentList::InstrumentList(int y, int width, int height, SDL_Renderer* renderer) {

    this->y = y;
    this->height = height;
    this->renderer = renderer;
    this->width = width;
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
    SDL_RenderLine(renderer, width, 0, width, height);

    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, 28,28,28,255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer, texture, nullptr, &dstRect);

    SDL_RenderTexture(renderer, noteTexture, nullptr, &dstRect);
}

void InstrumentList::renderInstrument(int i) {

    SDL_FRect instPos = {0, i*instrumentHeight + y, width, instrumentHeight + y};

    SDL_SetRenderDrawColor(renderer, 40,40,40,255);
    SDL_RenderFillRect(renderer, &instPos);
    SDL_SetRenderDrawColor(renderer, 20,20,20,255);
    SDL_RenderLine(renderer, 0, (i+1)*instrumentHeight-1+y, width, (i+1)*instrumentHeight-1+y);
}