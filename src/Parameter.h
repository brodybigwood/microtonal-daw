#pragma once

#include <vector>
#include <SDL3/SDL.h>
#include <string>
#include "Geometry.h"
#include "Bus.h"

struct Modulator {
    bool centered;
    float depth;
    float*& source; // can either be a node's connection input, or some internal buffer
    Connection* sourceConnection = nullptr;

    float operator[](size_t);

    Modulator(float*&, bool, float depth = 0.5, Connection* sourceConnection = nullptr);
};

struct Parameter {
    float value = 0; // the knob itself
    float defaultValue = 0;
    std::vector<Modulator*> modulators;
    
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
