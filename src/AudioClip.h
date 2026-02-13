#pragma once

#include <SDL3/SDL.h>
#include "GridElement.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#define DEPTH_RESOLUTION 16

class AudioClip : public GridElement {

public:
    AudioClip();
    ~AudioClip() override;

    std::string name = "Audio Region";

    void draw(SDL_Renderer*, float, int) override;

    json toJSON() override;
    void fromJSON(json) override;

    std::string filepath = "";
    float* buffer = nullptr;
    size_t num_samples = 0;
    int sampleRate = -1;

    void setFile(std::string);
};
