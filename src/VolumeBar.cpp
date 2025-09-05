#include "VolumeBar.h"
#include <cmath>

VolumeBar::VolumeBar() {};

void VolumeBar::render(SDL_Renderer* renderer, SDL_FRect* dstRect) {
    SDL_SetRenderDrawColor(renderer, 255 * amplitude, 255 * (1 - amplitude), 0, 255);
    float x = dstRect->x + (dstRect->w / 10.0f);
    float y = dstRect->y + (dstRect->h * 2 / 10.0f);
    float w = dstRect->w * 8 / 10.0f;
    float h = dstRect->h * 7 / 10.0f;

    SDL_FRect bounds{ x, y, w, h };

    SDL_FRect volRect{
        bounds.x,
        bounds.y + ( 1 - amplitude ) * bounds.h ,
        bounds.w,
        bounds.h * amplitude
    };

    SDL_RenderFillRect(renderer, &volRect);

    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderRect(renderer, &bounds);

}

void VolumeBar::process(audioData data) {
    
    amplitude = 0;

    auto stream = data.output;
    for(size_t ch = 0; ch < stream.numChannels; ch++) {
        auto buffer = stream.channels[ch].buffer;
        for(size_t s = 0; s < data.bufferSize; s++) {
            float abs = std::fabs(buffer[s]);
            if(abs > 1) {
                amplitude = 1;
                return;
            }
            if( abs > amplitude ) amplitude = abs;
        }
    }
}
