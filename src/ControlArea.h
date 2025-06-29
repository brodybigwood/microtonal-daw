#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include "Project.h"


#ifndef CONTROLAREA_H
#define CONTROLAREA_H

class Button;

class ControlArea {

    public:
        ControlArea(int height, int width, SDL_Renderer* renderer);
        ~ControlArea();

        Project* project;
        
        SDL_Renderer* renderer;
        int x = 0;
        int y = 0;
        int width;
        int height;

        float mouseX,mouseY;

        Button* hoveredButton = nullptr;

        void render();

        SDL_FRect dstRect;

        std::vector<Button*> buttons;

        void moveMouse(float mouseX, float mouseY);

        void handleInput(SDL_Event& e);

};

#endif
