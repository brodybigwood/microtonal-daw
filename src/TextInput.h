#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <functional>

class TextInput {
    public:

        SDL_Renderer* renderer;

        static TextInput* get();

        SDL_FRect dstRect;
        void tick(SDL_Event& e);

        std::function<void()> enter;

        bool active;

        std::string text = "";
};
