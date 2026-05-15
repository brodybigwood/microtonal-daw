#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>

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
    uint8_t noteBackground[4];

    uint8_t keyText[4];
    uint8_t keyWhite[4];
    uint8_t keyBlack[4];
    uint8_t keyWhiteActive[4];
    uint8_t keyBlackActive[4];

    uint8_t pianoSeparator[4];
    
    uint8_t playHead[4];

    uint8_t editorBackground[4];
    SDL_Color controlsBackground;
};

extern int lineWidth;

extern ColorCodes colors;

struct Cursors{
    SDL_Cursor* grabber;
    SDL_Cursor* pencil;
    SDL_Cursor* mover;
    SDL_Cursor* selector;
    SDL_Cursor* resize;
};

extern Cursors cursors;

struct Fonts{
    TTF_Font* mainFont;
};

extern Fonts fonts;

bool initFonts();

void createCursors();

/** Store a tooltip to be drawn later on top of everything. Only one at a time. */
void renderTooltip(SDL_Renderer* r, const std::string& text, float anchorX, float anchorY,
                   const SDL_FRect& bounds);

/** Draw the pending tooltip and clear it. Call at end of frame. */
void drawPendingTooltip();

/** Discard any pending tooltip. Call at start of frame. */
void clearPendingTooltip();

#endif // STYLES_H
