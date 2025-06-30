#include "styles.h"
#include <SDL3/SDL_mouse.h>
int lineWidth = 1;

ColorCodes colors {
    {84,82,82,255}, //bg
    {111,155,166,127}, //grid
    {56,74,86,255}, //subgrid

    {200,255,211,255}, //note
    {254,160,161,255},
    {136,176,141,255},
    {187,111,111,255},
    {50,64,108,100},

    {108,91,93,255},
    {238,242,250,255},
    {76,77,79,255},
    {219,223,231,255},
    {95,96,98,255},

    {50,66,76,255},

    {101, 182, 202, 255}, //playhead

    {72, 77, 78, 255}, //editor background
    {56, 56, 56, 255}
};

Cursors cursors{};

void createCursors() {
    cursors.grabber = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    cursors.pencil = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    cursors.mover = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
    cursors.selector = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    cursors.resize = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
}

Fonts fonts{
    nullptr
};

bool initFonts() {
    if(!TTF_Init()){
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_Init failed in styles.cpp: %s\n", SDL_GetError());
        return false;
    }
    fonts.mainFont = TTF_OpenFont("assets/fonts/SprinturaDemo.ttf", 12);
    if (fonts.mainFont) {
        return true;
    } else {
        return false;
    }
    
}
