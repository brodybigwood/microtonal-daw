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

void renderTooltip(SDL_Renderer* r, const std::string& text, float anchorX, float anchorY,
                   const SDL_FRect& bounds) {
    if (!fonts.mainFont || text.empty()) return;

    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text.c_str(), text.size(),
                                                SDL_Color{150, 165, 180, 200});
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) {
        SDL_DestroySurface(surf);
        return;
    }

    constexpr float pad = 5.f;
    constexpr float inset = 3.f;
    const float bw = static_cast<float>(surf->w) + pad * 2.f;
    const float bh = static_cast<float>(surf->h) + pad * 2.f;
    float bx = anchorX + 14.f;
    float by = anchorY - bh - 10.f;
    if (bx + bw > bounds.x + bounds.w - inset) bx = bounds.x + bounds.w - bw - inset;
    if (bx < bounds.x + inset) bx = bounds.x + inset;
    if (by < bounds.y + inset) by = bounds.y + inset;
    if (by + bh > bounds.y + bounds.h - inset) by = bounds.y + bounds.h - bh - inset;

    SDL_BlendMode prevBm;
    SDL_GetRenderDrawBlendMode(r, &prevBm);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    SDL_FRect bg{bx - 2.f, by - 2.f, bw + 4.f, bh + 4.f};
    SDL_SetRenderDrawColor(r, 26, 30, 36, 210);
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawColor(r, 72, 86, 102, 120);
    SDL_RenderRect(r, &bg);

    SDL_FRect tr{bx + pad - 2.f, by + pad - 2.f, static_cast<float>(surf->w), static_cast<float>(surf->h)};
    SDL_RenderTexture(r, tex, nullptr, &tr);

    SDL_SetRenderDrawBlendMode(r, prevBm);
    SDL_DestroyTexture(tex);
    SDL_DestroySurface(surf);
}
