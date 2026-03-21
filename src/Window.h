#pragma once
#include <SDL3/SDL.h>

class Window {
public:
    SDL_Window* window;
    SDL_Renderer* renderer;
    virtual void handleWindowInput(SDL_Event&) {}
};
