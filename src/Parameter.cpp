#include "Parameter.h"
#include "Node.h"
#include "NodeManager.h"
#include "UndoManager.h"
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
        return d * source[i];
    }
    return d * (source[i] + 1.0f);
}

Parameter::Parameter(float value, std::pair<std::vector<float>, std::vector<float>> bound) :
    value(value),
    defaultValue(value),
    vx(std::move(bound.first)),
    vy(std::move(bound.second)) {}

Parameter::Parameter(const Parameter& other)
    : value(other.value), defaultValue(other.defaultValue), label(other.label),
      clampOutput(other.clampOutput), mappedConnection(other.mappedConnection),
      lockMapping(other.lockMapping), vx(other.vx), vy(other.vy) {}

Parameter& Parameter::operator=(const Parameter& other) {
    if (this == &other) return *this;
    clearTextures();
    value = other.value;
    defaultValue = other.defaultValue;
    label = other.label;
    clampOutput = other.clampOutput;
    mappedConnection = other.mappedConnection;
    lockMapping = other.lockMapping;
    vx = other.vx;
    vy = other.vy;
    return *this;
}

Parameter::Parameter(Parameter&& other) noexcept
    : value(other.value), defaultValue(other.defaultValue), label(std::move(other.label)),
      clampOutput(other.clampOutput), mappedConnection(other.mappedConnection),
      lockMapping(other.lockMapping), vx(std::move(other.vx)), vy(std::move(other.vy)) {
    other.textures.clear();
}

Parameter& Parameter::operator=(Parameter&& other) noexcept {
    if (this == &other) return *this;
    clearTextures();
    value = other.value;
    defaultValue = other.defaultValue;
    label = std::move(other.label);
    clampOutput = other.clampOutput;
    mappedConnection = other.mappedConnection;
    lockMapping = other.lockMapping;
    vx = std::move(other.vx);
    vy = std::move(other.vy);
    other.textures.clear();
    return *this;
}

// Knob keeps a cached texture in its own member; the base's textures list holds
// &texture. Moving must re-point it at this instance and null the source.
Knob::Knob(Knob&& other) noexcept
    : Parameter(std::move(other)),
      thetaMin(other.thetaMin), thetaMax(other.thetaMax),
      label(std::move(other.label)), displayLabel(std::move(other.displayLabel)),
      labelColor(other.labelColor), filepath(std::move(other.filepath)),
      knobRect(other.knobRect), texture(other.texture),
      textureRenderer(other.textureRenderer),
      valueMin(other.valueMin), valueMax(other.valueMax), wheelStep(other.wheelStep),
      wheelVelocity(other.wheelVelocity) {
    other.texture = nullptr;
    other.textureRenderer = nullptr;
    other.textures.clear();
    textures.clear();
    if (texture) textures.push_back(&texture);
}

Knob& Knob::operator=(Knob&& other) noexcept {
    if (this == &other) return *this;
    Parameter::operator=(std::move(other));
    thetaMin = other.thetaMin;
    thetaMax = other.thetaMax;
    label = std::move(other.label);
    displayLabel = std::move(other.displayLabel);
    labelColor = other.labelColor;
    filepath = std::move(other.filepath);
    knobRect = other.knobRect;
    texture = other.texture;
    textureRenderer = other.textureRenderer;
    valueMin = other.valueMin;
    valueMax = other.valueMax;
    wheelStep = other.wheelStep;
    other.texture = nullptr;
    other.textureRenderer = nullptr;
    other.textures.clear();
    textures.clear();
    if (texture) textures.push_back(&texture);
    wheelVelocity = other.wheelVelocity;
    return *this;
}

void Parameter::beginDrag(Node* owner, size_t paramIndex) {
    ownerNode = owner;
    ownerIndex = paramIndex;
    dragActive = true;
    dragPushed = false;
    dragStartValue = value;
    dragLastValue = value;
}

void Parameter::dragToValue(float v) {
    const float nv = clampOutput ? std::clamp(v, 0.f, 1.f) : v;
    if (!dragActive) {
        value = nv;
        return;
    }
    if (std::fabs(nv - value) <= 0.0005f) return;
    // First change records the pre-drag value; later changes coalesce into it.
    const float old = dragPushed ? dragLastValue : dragStartValue;
    value = nv;
    if (ownerNode && ownerNode->project && ownerNode->project->um && ownerNode->nm) {
        std::vector<size_t> pp{ownerIndex};
        ownerNode->project->um->newAction(new SetParamValueUndoAction(
            ownerNode->project, ownerNode->nm->managerPath, static_cast<int>(ownerNode->id),
            std::move(pp), old, nv, label.empty() ? std::string("Parameter") : label));
        dragPushed = true;
        dragLastValue = nv;
    }
}

void Parameter::endDrag() {
    dragActive = false;
    ownerNode = nullptr;
    dragPushed = false;
}

float Parameter::operator[](size_t i) {
    if (mappedConnection && mappedConnection->is_connected && mappedConnection->buffer) {
        float v = mappedConnection->buffer[i] * 0.5f + 0.5f;
        return clampOutput ? std::clamp(v, 0.0f, 1.0f) : v;
    }
    return clampOutput ? std::clamp(value, 0.0f, 1.0f) : value;
}

void Parameter::clearTextures() {
    for (auto t : textures) {
        SDL_DestroyTexture(*t);
        *t = nullptr;
    }
    textures.clear();
}

Parameter::~Parameter() {
    clearTextures();
}

void Knob::render(SDL_Renderer* renderer) {
    if (texture && textureRenderer != renderer) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
        textures.clear();
    }
    if (!texture) {
        SDL_Surface* surface = IMG_Load(filepath.c_str());
        if (!surface) {
            SDL_Log("Failed to load image: %s", SDL_GetError());
        } else {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            textures.push_back(&texture);
            textureRenderer = renderer;
            SDL_DestroySurface(surface);
        }
    }

    const float span = valueMax - valueMin;
    const float norm = span > 0.0f ? std::clamp((value - valueMin) / span, 0.0f, 1.0f) : 0.0f;
    auto range = thetaMax - thetaMin;
    auto angle = thetaMin + norm * range;
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

    const std::string& shownLabel = displayLabel.empty() ? label : displayLabel;
    if (!shownLabel.empty() && fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, shownLabel.c_str(), 0, labelColor);
        if (surf) {
            SDL_Texture* textTex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_FRect tr{
                cx - static_cast<float>(surf->w) * 0.5f,
                knobRect.y + knobRect.h + 4.0f,
                static_cast<float>(surf->w),
                static_cast<float>(surf->h)
            };
            SDL_RenderTexture(renderer, textTex, nullptr, &tr);
            SDL_DestroyTexture(textTex);
            SDL_DestroySurface(surf);
        }
    }
}

Knob::Knob(float value, float x, float y, float r, std::string filepath, float thetaMin, float thetaMax, std::string label, SDL_Color labelColor) :
    Parameter(value, generateCircle(x, y, r)),
    knobRect{x - r, y - r, 2 * r, 2 * r},
    filepath(filepath),
    label(std::move(label)),
    labelColor(labelColor),
    thetaMin(thetaMin),
    thetaMax(thetaMax) {

}

void Knob::reposition(float newX, float newY, float newR) {
    knobRect = {newX - newR, newY - newR, 2.0f * newR, 2.0f * newR};
    auto poly = generateCircle(newX, newY, newR);
    vx = std::move(poly.first);
    vy = std::move(poly.second);
}

void Knob::handleInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_WHEEL:
            // Smooth raw SDL wheel motion so rapid scrolling is less jumpy.
            wheelVelocity = 0.75f * wheelVelocity + 0.25f * e.wheel.y;
            const float step = wheelStep > 0.0f ? wheelStep : 0.05f * (valueMax - valueMin);
            value = std::clamp(value + wheelVelocity * step, valueMin, valueMax);
        break;
    }
}

// --- Bar ---

Bar::Bar(float value, float x, float y, float w, float h)
    : Parameter(value, generateRect(x, y, w, h)), barRect{x, y, w, h} {}

void Bar::reposition(float x, float y, float w, float h) {
    barRect = {x, y, w, h};
    auto poly = generateRect(x, y, w, h);
    vx = std::move(poly.first);
    vy = std::move(poly.second);
}

void Bar::render(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 60, 60, 65, 255);
    SDL_RenderFillRect(renderer, &barRect);
    const float fillH = value * barRect.h;
    SDL_FRect fill{barRect.x, barRect.y + barRect.h - fillH, barRect.w, fillH};
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
    SDL_RenderFillRect(renderer, &fill);
    SDL_SetRenderDrawColor(renderer, 120, 120, 130, 255);
    SDL_RenderRect(renderer, &barRect);
}

void Bar::handleInputAt(SDL_Event& e, float mx, float my) {
    auto valueAt = [this](float y) {
        return std::clamp(1.f - (y - barRect.y) / barRect.h, 0.f, 1.f);
    };
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button != SDL_BUTTON_LEFT) return;
            if (mx < barRect.x - 4.f || mx > barRect.x + barRect.w + 4.f) return;
            if (my < barRect.y || my > barRect.y + barRect.h) return;
            beginDrag(ownerNode, ownerIndex);
            dragToValue(valueAt(my));
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (!dragging()) return;
            dragToValue(valueAt(my));
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (dragging()) endDrag();
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (mx < barRect.x || mx > barRect.x + barRect.w) return;
            if (my < barRect.y || my > barRect.y + barRect.h) return;
            beginDrag(ownerNode, ownerIndex);
            dragToValue(value + e.wheel.y * 0.05f);
            endDrag();
            break;
    }
}

// --- DropdownParameter ---

DropdownParameter::DropdownParameter(float value, float x, float y, float w, float h,
                                     std::vector<std::string> choices)
    : Parameter(value, generateRect(x, y, w, h))
    , boxRect{x, y, w, h}
    , choices(std::move(choices)) {}

float DropdownParameter::operator[](size_t i) {
    float v = value;
    v = std::clamp(v, 0.0f, 1.0f);
    int n = static_cast<int>(choices.size());
    if (n > 1) {
        float step = 1.0f / static_cast<float>(n - 1);
        v = std::round(v / step) * step;
    }
    return v;
}

size_t DropdownParameter::getChoiceIndex() const {
    if (choices.size() <= 1) return 0;
    return std::clamp(static_cast<size_t>(std::round(value * static_cast<float>(choices.size() - 1))),
                      static_cast<size_t>(0), choices.size() - 1);
}

const std::string& DropdownParameter::getChoice() const {
    return choices[getChoiceIndex()];
}

void DropdownParameter::render(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 42, 42, 47, 255);
    SDL_RenderFillRect(renderer, &boxRect);
    SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
    SDL_RenderRect(renderer, &boxRect);

    if (!fonts.mainFont) return;
    const std::string& label = getChoice();
    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, label.c_str(), 0, SDL_Color{220, 220, 220, 255});
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    const float scale = 1.6f;
    const float tw = static_cast<float>(surf->w) * scale;
    const float th = static_cast<float>(surf->h) * scale;
    SDL_FRect tr{boxRect.x + (boxRect.w - tw) * 0.5f, boxRect.y + (boxRect.h - th) * 0.5f, tw, th};
    SDL_RenderTexture(renderer, tex, nullptr, &tr);
    SDL_DestroyTexture(tex);
    SDL_DestroySurface(surf);
}

void DropdownParameter::handleInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_WHEEL:
            if (choices.size() <= 1) return;
            {
                int idx = static_cast<int>(getChoiceIndex());
                idx = std::clamp(idx + (e.wheel.y > 0 ? 1 : -1), 0, static_cast<int>(choices.size()) - 1);
                value = static_cast<float>(idx) / static_cast<float>(choices.size() - 1);
            }
            break;
    }
}

void DropdownParameter::reposition(float newX, float newY, float newW, float newH) {
    boxRect = {newX, newY, newW, newH};
    auto poly = generateRect(newX, newY, newW, newH);
    vx = std::move(poly.first);
    vy = std::move(poly.second);
}
