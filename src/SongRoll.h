#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <cmath>
#include "styles.h"

#ifndef SONGROLL_H
#define SONGROLL_H

class SongRoll {

    public:
    SongRoll(int x, int y, int width, int height, SDL_Renderer* renderer);
        ~SongRoll();
        
        SDL_Renderer* renderer;
        int x = 0;
        int y = 0;
        int width;
        int height;

        int cellHeight = 50;
        int cellWidth = 20;
        int barWidth = 80;
        void render();

        SDL_Texture* texture;
        SDL_Texture* gridTexture;

        SDL_FRect dstRect;

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


};

#endif