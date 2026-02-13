#include "AudioClip.h"
#include <cmath>
#include "SDL3_gfx/SDL3_gfxPrimitives.h"
#include <iostream>
#include <sndfile.h>

AudioClip::AudioClip() {
    type = ElementType::audioClip;
}

AudioClip::~AudioClip() {
    if (buffer) delete[] buffer;
}

json AudioClip::toJSON() {
    json j;

    j["name"] = name;
    j["id"] = id;
    j["type"] = ElementType::audioClip;
    j["positions"] = GridElement::toJSON();

    j["filepath"] = filepath;

    return j;
}

void AudioClip::fromJSON(json j) {
    name = j["name"];
    id = j["id"];
    type = j["type"];
    GridElement::fromJSON(j["positions"]);
}

void AudioClip::draw(SDL_Renderer* renderer, float pixelsPerSecond, int h) {

    if (!buffer) return;

    if (this->pixelsPerSecond != pixelsPerSecond || this->h != h) {
        this->pixelsPerSecond = pixelsPerSecond;
        this->h = h;

        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    if (!texture) {
        
        int sampleRate = 48000;
        float samplesPerPixel = sampleRate / pixelsPerSecond;

        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 16384, h);

        if (!texture) std::cout << SDL_GetError() << std::endl;       
        std::cout << "created texture with dimensions 16384x" << h << std::endl;
 
        SDL_SetRenderTarget(renderer, texture);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        int center = h / 2;

        float wH = 0.75; // amplitude 1 is 75% height

        for (float i = 1; i < 16384; ++i ) {
            int x1 = i-1;
            int x2 = i;
            size_t sample1 = x1 * samplesPerPixel;
            size_t sample2 = x2 * samplesPerPixel;

            if (sample2 >= num_samples) break;
            
            int y1 = center - (int)(buffer[sample1] * center * wH);
            int y2 = center - (int)(buffer[sample2] * center * wH);

            aalineRGBA(renderer, x1, y1, x2, y2, 255, 255, 255, 255);
            aalineRGBA(renderer, x1, y1, x2, y2, 255, 255, 255, 255);
            aalineRGBA(renderer, x1, y1+1, x2, y2+1, 255, 255, 255 ,255);
        }

        SDL_SetRenderTarget(renderer, NULL);
    }
}

void AudioClip::setFile(std::string file_path) {

    SF_INFO sfinfo{};
    SNDFILE* sndfile = sf_open(file_path.c_str(), SFM_READ, &sfinfo);
    if (!sndfile) {
        std::cerr << "cant open audio file: " << sf_strerror(NULL) << std::endl;
        return;
    }

    if (buffer) delete[] buffer;

    buffer = new float[sfinfo.frames];
    float* interleaved = new float[sfinfo.frames * sfinfo.channels * sizeof(float)];
    num_samples = sf_readf_float(sndfile, interleaved, sfinfo.frames);

    for (size_t i = 0; i < num_samples; ++i) {
        buffer[i] = interleaved[i * sfinfo.channels]; // audioClips only support mono currently, so take the first channel
    }

    filepath = file_path;
    sf_close(sndfile);
    delete[] interleaved;
}
