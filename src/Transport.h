#pragma once
#include <SDL3/SDL.h>

class Project;
class Button;
class GridView;

class Transport {
    public:
        static constexpr float kTimelineHeight = 18.f;

        Transport(GridView*);
        ~Transport();

        GridView* view;
        SDL_FRect* dstRect;

        int mouseX;
        int mouseY;

        void render(SDL_Renderer*);

        void moveMouse(float, float);
        void clickMouse();
        void handleMotion();

        bool draggingTimeline = false;
};
