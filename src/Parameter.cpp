#include "Parameter.h"
#include <algorithm>
#include "SDL3_gfx/SDL3_gfxPrimitives.h"

Modulator::Modulator(float*& source, bool centered, float depth) : 
    source(source),
    centered(centered),
    depth(depth) {}

float Modulator::operator[](size_t i) {
    return depth * (source[i] - 0.5 * centered);
}

Parameter::Parameter(float value, SDL_FRect rect) :
    value(value),
    rect(rect) {}

float Parameter::operator[](size_t i) {
    float v = value;
    for (auto& m : modulators) v += (*m)[i];
    return std::clamp(v, 0.0f, 1.0f);
}

void Parameter::render(SDL_Renderer* renderer) {
    filledCircleRGBA(renderer, rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f, rect.h, 0, 0, 255, 255);
    aacircleRGBA(renderer, rect.x + rect.w / 2.0f, rect.y + rect.h / 2.0f, rect.h, 0, 0, 0, 255);
}
