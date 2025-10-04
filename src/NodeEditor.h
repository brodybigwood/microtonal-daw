#pragma once

#include <SDL3/SDL.h>

class NodeEditor {
    public:
        NodeEditor();
        ~NodeEditor();

        static NodeEditor* get();

        void tick();
        void handleInput(SDL_Event&);

    private:
        SDL_Window* window;
        SDL_Renderer* renderer;
};
