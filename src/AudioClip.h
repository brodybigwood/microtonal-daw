#pragma once

#include <SDL3/SDL.h>
#include "GridElement.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#define DEPTH_RESOLUTION 16

class AudioClip : public GridElement {

public:
    AudioClip(Project*, ArrangerNode*);
    ~AudioClip() override;


    void draw(SDL_Renderer*, float, int) override;
    uint64_t thumbnailSignature() const override;
    void drawThumbnail(SDL_Renderer*, int, int) override;

    json toJSON() override;
    void fromJSON(json) override;

    std::string filepath = "";
    float* buffer = nullptr;
    size_t num_samples = 0;
    int sampleRate = 0;
    int numChannels = 1;

    void setFile(std::string);
};
