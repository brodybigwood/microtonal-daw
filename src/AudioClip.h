#pragma once

#include <SDL3/SDL.h>
#include "GridElement.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class AudioClip : public GridElement {

public:
    ~AudioClip() override;

    std::string name = "Audio Region";

    void draw(SDL_Renderer*) override;

    json toJSON() override;
    void fromJSON(json) override;

    std::string filepath = "";
    float* buffer = nullptr;
    int sampleRate = -1;

    void setFile(std::string);
};
