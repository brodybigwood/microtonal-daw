#pragma once

#include <vector>
#include <SDL3/SDL.h>
#include <string>
#include "Geometry.h"

struct Modulator {
    bool centered;
    float depth;
    float*& source; // can either be a node's connection input, or some internal buffer

    float operator[](size_t);

    Modulator(float*&, bool, float depth = 0.5);
};

struct Parameter {
    float value = 0; // the knob itself
    std::vector<Modulator*> modulators;
    
    float operator[](size_t);

    void addModulator(Modulator*);

    std::vector<float> vx;
    std::vector<float> vy;

    virtual void render(SDL_Renderer*) {}
    virtual void handleInput(SDL_Event&) {}

    Parameter(float, std::pair<std::vector<float>, std::vector<float>>);
    virtual ~Parameter() {}
};

struct Knob : Parameter {

    void handleInput(SDL_Event&) override;
    std::string filepath;        
    SDL_FRect knobRect;
    SDL_Texture* texture = nullptr;
    void render(SDL_Renderer*) override;
    Knob(float, float, float, float, std::string);
    ~Knob();
};
