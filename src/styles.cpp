#include "styles.h"
#include "Settings.h"
#include <SDL3/SDL_mouse.h>
int lineWidth = 1;

ColorCodes colors {
    {66, 84, 95, 255}, //bg
    {29, 47, 57, 255}, //grid

    {200,255,211,255}, //note
    {254,160,161,255},
    {136,176,141,255},
    {187,111,111,255},
    {50,64,108,100},

    {238,242,250,255}, //keyWhite

    {101, 182, 202, 255}, //playhead

    {38, 42, 48, 255},  //trackBackground
    {48, 52, 58, 255},  //trackBody
    {60, 64, 72, 255},  //trackBorder
    {210, 90, 90, 255}, //trackAudio
    {90, 200, 130, 255},//trackNotes
    {210, 190, 55, 255},//trackAutomation
    {32, 35, 40, 255},  //nodeGraphBg
    {38, 42, 48, 255}   //elementListBg
};

Cursors cursors{};

namespace {
void setColorField(uint8_t* dst, const nlohmann::json& j, const char* key) {
    if (j.contains(key) && j[key].is_array() && j[key].size() >= 4) {
        for (int i = 0; i < 4; ++i) dst[i] = j[key][i].get<uint8_t>();
    }
}
} // namespace

void ColorCodes::loadFromJson(const nlohmann::json& j) {
    setColorField(background, j, "background");
    setColorField(grid, j, "grid");
    setColorField(note, j, "note");
    setColorField(noteSelected, j, "noteSelected");
    setColorField(noteBorder, j, "noteBorder");
    setColorField(noteSelectedBorder, j, "noteSelectedBorder");
    setColorField(noteBackground, j, "noteBackground");
    setColorField(keyWhite, j, "keyWhite");
    setColorField(playHead, j, "playHead");
    setColorField(trackBackground, j, "trackBackground");
    setColorField(trackBody, j, "trackBody");
    setColorField(trackBorder, j, "trackBorder");
    setColorField(trackAudio, j, "trackAudio");
    setColorField(trackNotes, j, "trackNotes");
    setColorField(trackAutomation, j, "trackAutomation");
    setColorField(nodeGraphBg, j, "nodeGraphBg");
    setColorField(elementListBg, j, "elementListBg");
}

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

// --- Deferred tooltip (single, drawn at end of frame) ---

static bool g_tooltipPending = false;
static std::string g_tooltipText;
static float g_tooltipAnchorX = 0.f;
static float g_tooltipAnchorY = 0.f;
static SDL_FRect g_tooltipBounds{};
static SDL_Renderer* g_tooltipRenderer = nullptr;

void renderTooltip(SDL_Renderer* r, const std::string& text, float anchorX, float anchorY,
                   const SDL_FRect& bounds) {
    if (text.empty()) return;
    g_tooltipPending = true;
    g_tooltipText = text;
    g_tooltipAnchorX = anchorX;
    g_tooltipAnchorY = anchorY;
    g_tooltipBounds = bounds;
    g_tooltipRenderer = r;
}

void clearPendingTooltip() {
    g_tooltipPending = false;
}

void drawPendingTooltip() {
    if (!g_tooltipPending || !fonts.mainFont || !g_tooltipRenderer) return;
    SDL_Renderer* r = g_tooltipRenderer;
    g_tooltipPending = false;

    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, g_tooltipText.c_str(), g_tooltipText.size(),
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
    float bx = g_tooltipAnchorX + 14.f;
    float by = g_tooltipAnchorY - bh - 10.f;
    if (bx + bw > g_tooltipBounds.x + g_tooltipBounds.w - inset) bx = g_tooltipBounds.x + g_tooltipBounds.w - bw - inset;
    if (bx < g_tooltipBounds.x + inset) bx = g_tooltipBounds.x + inset;
    if (by < g_tooltipBounds.y + inset) by = g_tooltipBounds.y + inset;
    if (by + bh > g_tooltipBounds.y + g_tooltipBounds.h - inset) by = g_tooltipBounds.y + g_tooltipBounds.h - bh - inset;

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

static ColorCodes defaultColors = colors;

const ColorCodes& getDefaultColors() { return defaultColors; }

void loadColorsFromSettings() {
    auto& s = Settings::instance();
    std::string presetName = s.currentColorPreset();
    auto presets = s.getColorPresets();
    if (presets.contains(presetName) && presets[presetName].contains("colors"))
        colors.loadFromJson(presets[presetName]["colors"]);
}
