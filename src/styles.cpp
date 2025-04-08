#include "styles.h"
int lineWidth = 1;

ColorCodes colors {
    {66,84,95,255},
    {46,64,75,255},
    {56,74,86,255},

    {200,255,211,255},
    {254,160,161,255},
    {136,176,141,255},
    {187,111,111,255},

    {108,91,93,255},
    {238,242,250,255},
    {76,77,79,255},
    {219,223,231,255},
    {95,96,98,255},

    {50,66,76,255}
};

Cursors cursors{
    SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER),
    SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT),
    SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE),
    SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT),
};