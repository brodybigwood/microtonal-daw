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
    std::vector<Modulator*> modulators;
    bool clampOutput = true;

    float operator[](size_t);

    void addModulator(Modulator*);

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

    void handleInput(SDL_Event&) override;
    std::string filepath;
    SDL_FRect knobRect;
    SDL_Texture* texture = nullptr;
    float wheelVelocity = 0.0f;
    void render(SDL_Renderer*) override;
    Knob(float, float, float, float, std::string, float, float, std::string label = "");
};
