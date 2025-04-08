#include <SDL3/SDL.h>

#ifndef STYLES_H
#define STYLES_H


struct ColorCodes{
    uint8_t background[4];
    uint8_t grid[4];
    uint8_t subGrid[4];

    uint8_t note[4];
    uint8_t noteSelected[4];
    uint8_t noteBorder[4];
    uint8_t noteSelectedBorder[4];

    uint8_t keyText[4];
    uint8_t keyWhite[4];
    uint8_t keyBlack[4];
    uint8_t keyWhiteActive[4];
    uint8_t keyBlackActive[4];

    uint8_t pianoSeparator[4];
    
};

extern int lineWidth;

extern ColorCodes colors;

struct Cursors{
    SDL_Cursor* grabber;
    SDL_Cursor* pencil;
    SDL_Cursor* mover;
    SDL_Cursor* selector;
};

extern Cursors cursors;

#endif // STYLES_H