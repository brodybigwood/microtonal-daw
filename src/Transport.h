#pragma once
#include <SDL3/SDL.h>

class Project;
class Button;
class GridView;

class Transport {
    public:
        Transport(GridView*);
        ~Transport();

        GridView* view;
        SDL_Renderer* renderer;
        SDL_FRect* dstRect;

        int mouseX;
        int mouseY;

        void render();

        void moveMouse(float, float);
        void clickMouse();

        Button* togglePlay;
        Button* stop;
};
