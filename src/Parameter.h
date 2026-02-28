#pragma once

#include <vector>
#include <SDL3/SDL.h>

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

    SDL_FRect rect;
    void render(SDL_Renderer*);

    Parameter(float, SDL_FRect);
};
