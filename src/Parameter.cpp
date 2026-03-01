#include "Parameter.h"
#include <algorithm>
#include "SDL3_gfx/SDL3_gfxPrimitives.h"
#include <SDL3_image/SDL_image.h>
#include <iostream>

Modulator::Modulator(float*& source, bool centered, float depth) : 
    source(source),
    centered(centered),
    depth(depth) {}

float Modulator::operator[](size_t i) {
    return depth * (source[i] - 0.5 * centered);
}

Parameter::Parameter(float value, std::pair<std::vector<float>, std::vector<float>> bound) :
    value(value),
    vx(std::move(bound.first)),
    vy(std::move(bound.second)) {}

float Parameter::operator[](size_t i) {
    float v = value;
    for (auto& m : modulators) v += (*m)[i];
    return std::clamp(v, 0.0f, 1.0f);
}

void Knob::render(SDL_Renderer* renderer) {
    if (!texture) {
        SDL_Surface* surface = IMG_Load(filepath.c_str());
        if (!surface) {
            SDL_Log("Failed to load image: %s", SDL_GetError());
        } else {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_DestroySurface(surface);
        }
    }

    SDL_RenderTexture(renderer, texture, NULL, &knobRect);
}

Knob::Knob(float value, float x, float y, float r, std::string filepath) : 
    Parameter(value, generateCircle(x, y, r)), 
    knobRect{x - r, y - r, 2 * r, 2 * r}, 
    filepath(filepath) {

}

Knob::~Knob() {
    if (texture) SDL_DestroyTexture(texture);
}

void Knob::handleInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_WHEEL:
            value = std::clamp(value + e.wheel.y * 0.05f, 0.0f, 1.0f);
        break;
    }
}
