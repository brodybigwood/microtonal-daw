#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <cmath>
#include "styles.h"
#include "Project.h"
#include <iostream>
#include "WindowHandler.h"
#include "Playhead.h"

#ifndef SONGROLL_H
#define SONGROLL_H

class WindowHandler;  // forward declaration


class SongRoll {

    public:
    SongRoll(int x, int y, int width, int height, SDL_Renderer* renderer, Project* project, WindowHandler* windowHandler);
        ~SongRoll();
        
        WindowHandler* windowHandler;
        SDL_Renderer* renderer;
        int x = 0;
        int y = 0;
        int width;
        int height;

        Project* project;

        int cellHeight = 50;
        int cellWidth = 20;
        int barWidth = 80;
        void render();

        SDL_Texture* texture;
        SDL_Texture* gridTexture;
        SDL_Texture* regionTexture;
        SDL_Texture* playHeadTexture;

        SDL_FRect dstRect;

        SDL_FRect regionRect;

        float mouseX = 0;
        float mouseY = 0;

        int scrollX = 0;
        int scrollY = 0;

        bool lmb = false;
        bool rmb = false;

        int scrollSensitivity = 10;

        int scaleSensitivity = 1.1;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
        void handleInput(SDL_Event& e);

        bool isShiftPressed = false;
        bool isAltPressed = false;
        bool isCtrlPressed = false;

        void RenderGridTexture();

        void renderRegions();
        void renderRegion(int);

        void getHoveredRegion();

        int hoveredRegion = -1;

        void moveMouse(float, float);

        void clickMouse(SDL_Event& e);

        Playhead* playHead;
};

#endif