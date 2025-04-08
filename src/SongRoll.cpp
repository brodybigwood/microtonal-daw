#include "SongRoll.h"


SongRoll::SongRoll(int x, int y, int width, int height, SDL_Renderer* renderer) {

    this->width = width;
    this->height = height;
    this->renderer = renderer;

    this->x = x;
    this->y = y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    dstRect = {x, y, width, height};



    
}

void SongRoll::render() {
    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
}


SongRoll::~SongRoll() {

}
