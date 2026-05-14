#include "Parameter.h"
#include <algorithm>
#include "SDL3_gfx/SDL3_gfxPrimitives.h"
#include <SDL3_image/SDL_image.h>
#include <iostream>
#include <cmath>
#include "styles.h"

Modulator::Modulator(float*& source, bool centered, std::pair<std::vector<float>, std::vector<float>> depthPolygon, float depthValue, Connection* sourceConnection) :
    source(source),
    centered(centered),
    depth(depthValue, std::move(depthPolygon)),
    sourceConnection(sourceConnection) {
    depth.clampOutput = false;
}

float Modulator::operator[](size_t i) {
    if (!source) return 0.0f;
    float d = depth[i];
    if (centered) {
        return d * (2.0f * source[i] - 1.0f);
    }
    return d * source[i];
}

Parameter::Parameter(float value, std::pair<std::vector<float>, std::vector<float>> bound) :
    value(value),
    defaultValue(value),
    vx(std::move(bound.first)),
    vy(std::move(bound.second)) {}

float Parameter::operator[](size_t i) {
    float v = value;
    for (auto& m : modulators) v += (*m)[i];
    return clampOutput ? std::clamp(v, 0.0f, 1.0f) : v;
}

void Parameter::addModulator(Modulator* m) {
    if (!m) return;
    modulators.push_back(m);
}

void Parameter::clearTextures() {
    for (auto t : textures) {
        SDL_DestroyTexture(*t);
        *t = nullptr;
    }
    textures.clear();
}

Parameter::~Parameter() {
    for (auto* m : modulators) {
        delete m;
    }
    modulators.clear();
    clearTextures();
}

void Knob::render(SDL_Renderer* renderer) {
    if (!texture) {
        SDL_Surface* surface = IMG_Load(filepath.c_str());
        if (!surface) {
            SDL_Log("Failed to load image: %s", SDL_GetError());
        } else {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            textures.push_back(&texture);
            SDL_DestroySurface(surface);
        }
    }

    auto range = thetaMax - thetaMin;
    auto angle = thetaMin + value * range;
    SDL_FPoint center = { knobRect.w/2, knobRect.h/2 };

    SDL_RenderTextureRotated(renderer, texture, NULL, &knobRect, angle, &center, SDL_FLIP_NONE);

    const float cx = knobRect.x + knobRect.w * 0.5f;
    const float cy = knobRect.y + knobRect.h * 0.5f;
    const float rOuter = knobRect.w * 0.58f;
    const float rInner = knobRect.w * 0.42f;
    auto toRad = [](float deg) { return deg * static_cast<float>(M_PI) / 180.0f; };
    const float aMin = toRad(thetaMin);
    const float aMax = toRad(thetaMax);
    // Bound markers are offset to align with knob art rotation origin.
    const float x1Min = cx - std::sin(aMin) * rInner;
    const float y1Min = cy - std::cos(aMin) * rInner;
    const float x2Min = cx - std::sin(aMin) * rOuter;
    const float y2Min = cy - std::cos(aMin) * rOuter;
    const float x1Max = cx - std::sin(aMax) * rInner;
    const float y1Max = cy - std::cos(aMax) * rInner;
    const float x2Max = cx - std::sin(aMax) * rOuter;
    const float y2Max = cy - std::cos(aMax) * rOuter;

    thickLineRGBA(renderer,
        static_cast<Sint16>(x1Min), static_cast<Sint16>(y1Min),
        static_cast<Sint16>(x2Min), static_cast<Sint16>(y2Min),
        4, 20, 20, 20, 255);
    thickLineRGBA(renderer,
        static_cast<Sint16>(x1Max), static_cast<Sint16>(y1Max),
        static_cast<Sint16>(x2Max), static_cast<Sint16>(y2Max),
        4, 20, 20, 20, 255);

    if (!label.empty() && fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, label.c_str(), 0, SDL_Color{0, 0, 0, 255});
        if (surf) {
            SDL_Texture* textTex = SDL_CreateTextureFromSurface(renderer, surf);
            // Keep label visual size consistent across differently sized knobs.
            const float referenceDiameter = 290.0f; // filter knob diameter
            const float scale = std::clamp(2.0f * (knobRect.w / referenceDiameter), 2.0f, 3.2f);
            const float tw = static_cast<float>(surf->w) * scale;
            const float th = static_cast<float>(surf->h) * scale;
            SDL_FRect tr{
                cx - tw * 0.5f,
                knobRect.y + knobRect.h + 16.0f,
                tw,
                th
            };
            SDL_RenderTexture(renderer, textTex, nullptr, &tr);
            SDL_DestroyTexture(textTex);
            SDL_DestroySurface(surf);
        }
    }
}

Knob::Knob(float value, float x, float y, float r, std::string filepath, float thetaMin, float thetaMax, std::string label) : 
    Parameter(value, generateCircle(x, y, r)), 
    knobRect{x - r, y - r, 2 * r, 2 * r}, 
    filepath(filepath),
    label(std::move(label)),
    thetaMin(thetaMin),
    thetaMax(thetaMax) {

}

void Knob::handleInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_WHEEL:
            // Smooth raw SDL wheel motion so rapid scrolling is less jumpy.
            wheelVelocity = 0.75f * wheelVelocity + 0.25f * e.wheel.y;
            value = std::clamp(value + wheelVelocity * 0.05f, 0.0f, 1.0f);
        break;
    }
}
