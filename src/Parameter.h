#pragma once

#include <vector>
#include <SDL3/SDL.h>
#include <string>
#include "Geometry.h"
#include "Bus.h"

struct Modulator;

struct Parameter {
    float value = 0;
    float defaultValue = 0;
    std::string label;
    bool clampOutput = true;
    Connection* mappedConnection = nullptr;
    bool lockMapping = false;

    virtual float operator[](size_t);

    std::vector<float> vx;
    std::vector<float> vy;

    virtual void render(SDL_Renderer*) {}
    virtual void handleInput(SDL_Event&) {}

    Parameter(float, std::pair<std::vector<float>, std::vector<float>>);
    ~Parameter();

    std::vector<SDL_Texture**> textures;
    void clearTextures();
};

struct Modulator {
    bool centered;
    Parameter depth;
    float*& source;
    Connection* sourceConnection = nullptr;

    float operator[](size_t);

    Modulator(float*&, bool, std::pair<std::vector<float>, std::vector<float>> depthPolygon, float depthValue = 0.5, Connection* sourceConnection = nullptr);
};

struct Knob : Parameter {

    float thetaMin;
    float thetaMax;
    std::string label;
    SDL_Color labelColor{0, 0, 0, 255};

    void handleInput(SDL_Event&) override;
    std::string filepath;
    SDL_FRect knobRect;
    SDL_Texture* texture = nullptr;
    float wheelVelocity = 0.0f;
    void render(SDL_Renderer*) override;
    void reposition(float newX, float newY, float newR);
    Knob(float, float, float, float, std::string, float, float, std::string label = "",
         SDL_Color labelColor = {0, 0, 0, 255});
};

struct DropdownParameter : Parameter {
    std::vector<std::string> choices;
    SDL_FRect boxRect;

    float operator[](size_t) override;
    void render(SDL_Renderer*) override;
    void handleInput(SDL_Event&) override;

    size_t getChoiceIndex() const;
    const std::string& getChoice() const;
    void reposition(float newX, float newY, float newW, float newH);

    DropdownParameter(float value, float x, float y, float w, float h,
                      std::vector<std::string> choices);
};
