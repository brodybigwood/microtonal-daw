#include "VolumeBar.h"
#include <cmath>

VolumeBar::VolumeBar() {};

void VolumeBar::render(SDL_Renderer* renderer, SDL_FRect* dstRect) {
    SDL_SetRenderDrawColor(renderer, 255, 20, 20, 255);
    float w = dstRect->w / 2;
    float h = dstRect->h * (3 / 4.0f) * amplitude;
    SDL_FRect volRect{
        dstRect->x + (dstRect->w/2) - w/2,
        dstRect->y + (dstRect->h/2) + (h/2) - h,
        w,
        h
    };
    SDL_RenderFillRect(renderer, &volRect);

}

void VolumeBar::process(audioData data) {
    
    amplitude = 0;

    auto stream = data.output;
    for(size_t ch = 0; ch < stream.numChannels; ch++) {
        auto buffer = stream.channels[ch].buffer;
        for(size_t s = 0; s < data.bufferSize; s++) {
            float abs = std::fabs(buffer[s]);
            if( abs > amplitude ) amplitude = abs;
        }
    }
}
