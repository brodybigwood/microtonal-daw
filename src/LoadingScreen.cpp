#include "LoadingScreen.h"

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "styles.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace loading {

namespace {

struct Vtx {
    float x, y;
    SDL_FColor c;
};

void geom(SDL_Renderer* r, std::vector<SDL_Vertex>& v, std::vector<int>& idx, SDL_BlendMode blend) {
    SDL_SetRenderDrawBlendMode(r, blend);
    SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()),
                       idx.empty() ? nullptr : idx.data(), static_cast<int>(idx.size()));
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void fillRect(SDL_Renderer* r, const SDL_FRect& rc, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rc);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void rectBorder(SDL_Renderer* r, const SDL_FRect& rc, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderRect(r, &rc);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// Radial gradient disc (centre colour to transparent rim).
void glassDisc(SDL_Renderer* r, float cx, float cy, float rad, SDL_Color inner, SDL_Color outer) {
    const int segs = 48;
    std::vector<SDL_Vertex> v;
    v.reserve(static_cast<size_t>(segs) + 2);
    SDL_FColor ci{inner.r / 255.f, inner.g / 255.f, inner.b / 255.f, inner.a / 255.f};
    SDL_FColor co{outer.r / 255.f, outer.g / 255.f, outer.b / 255.f, outer.a / 255.f};
    v.push_back({{cx, cy}, ci});
    for (int i = 0; i <= segs; ++i) {
        const float a = static_cast<float>(i) * 2.f * static_cast<float>(M_PI) / segs;
        v.push_back({{cx + std::cos(a) * rad, cy + std::sin(a) * rad}, co});
    }
    std::vector<int> idx;
    idx.reserve(static_cast<size_t>(segs) * 3);
    for (int i = 0; i < segs; ++i) {
        idx.push_back(0);
        idx.push_back(i + 1);
        idx.push_back(i + 2);
    }
    geom(r, v, idx, SDL_BLENDMODE_BLEND);
}

// Soft glow around a pair of arcs, used for the rotating glass ring.
void ringArc(SDL_Renderer* r, float cx, float cy, float rad, float start, float span,
             float thickness, SDL_Color c, int steps) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (int i = 0; i <= steps; ++i) {
        const float a = start + span * static_cast<float>(i) / steps;
        const float ca = std::cos(a), sa = std::sin(a);
        const float inner = rad - thickness * 0.5f;
        const float outer = rad + thickness * 0.5f;
        SDL_FColor col{c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
        v.push_back({{cx + ca * inner, cy + sa * inner}, col});
        v.push_back({{cx + ca * outer, cy + sa * outer}, col});
    }
    for (int i = 0; i < steps; ++i) {
        const int b = i * 2;
        idx.push_back(b);
        idx.push_back(b + 1);
        idx.push_back(b + 2);
        idx.push_back(b + 1);
        idx.push_back(b + 3);
        idx.push_back(b + 2);
    }
    geom(r, v, idx, SDL_BLENDMODE_BLEND);
}

// Rounded rectangle filled with a vertical white gloss that fades out.
void glossPanel(SDL_Renderer* r, const SDL_FRect& rc, float radius,
                SDL_Color fill, SDL_Color border, SDL_Color gloss) {
    fillRect(r, {rc.x + radius, rc.y, rc.w - radius * 2.f, rc.h}, fill);
    fillRect(r, {rc.x, rc.y + radius, rc.w, rc.h - radius * 2.f}, fill);
    glassDisc(r, rc.x + radius, rc.y + radius, radius, fill, {fill.r, fill.g, fill.b, 0});
    glassDisc(r, rc.x + rc.w - radius, rc.y + radius, radius, fill, {fill.r, fill.g, fill.b, 0});
    glassDisc(r, rc.x + radius, rc.y + rc.h - radius, radius, fill, {fill.r, fill.g, fill.b, 0});
    glassDisc(r, rc.x + rc.w - radius, rc.y + rc.h - radius, radius, fill,
              {fill.r, fill.g, fill.b, 0});

    // Top glass sheen.
    const float glossH = rc.h * 0.48f;
    std::vector<SDL_Vertex> v{
        {{rc.x + radius * 0.4f, rc.y}, {gloss.r / 255.f, gloss.g / 255.f, gloss.b / 255.f, gloss.a / 255.f}},
        {{rc.x + rc.w - radius * 0.4f, rc.y}, {gloss.r / 255.f, gloss.g / 255.f, gloss.b / 255.f, gloss.a / 255.f}},
        {{rc.x + rc.w - radius * 0.4f, rc.y + glossH}, {gloss.r / 255.f, gloss.g / 255.f, gloss.b / 255.f, 0.f}},
        {{rc.x + radius * 0.4f, rc.y + glossH}, {gloss.r / 255.f, gloss.g / 255.f, gloss.b / 255.f, 0.f}},
    };
    std::vector<int> idx{0, 1, 2, 0, 2, 3};
    geom(r, v, idx, SDL_BLENDMODE_BLEND);

    rectBorder(r, rc, border);
}

SDL_Texture* cachedText(SDL_Renderer* r, const std::string& text, int size, SDL_Color c) {
    static std::map<std::string, SDL_Texture*> cache;
    char key[512];
    std::snprintf(key, sizeof(key), "%d:%d,%d,%d,%d:%s", size, c.r, c.g, c.b, c.a, text.c_str());
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    if (!fonts.mainFont) return nullptr;
    TTF_Font* font = fonts.mainFont;
    int old = TTF_GetFontSize(font);
    TTF_SetFontSize(font, size);
    SDL_Surface* s = TTF_RenderText_Blended(font, text.c_str(), 0, c);
    TTF_SetFontSize(font, old);
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    cache[key] = t;
    return t;
}

void drawText(SDL_Renderer* r, const std::string& text, int size, SDL_Color c,
              float x, float y, bool centered) {
    SDL_Texture* t = cachedText(r, text, size, c);
    if (!t) return;
    float tw = 0, th = 0;
    SDL_GetTextureSize(t, &tw, &th);
    SDL_FRect dst{x - (centered ? tw * 0.5f : 0.f), y, tw, th};
    SDL_RenderTexture(r, t, nullptr, &dst);
}

} // namespace

void render(SDL_Renderer* r, int w, int h, double seconds, float progress,
            const std::string& stage) {
    const float W = static_cast<float>(w);
    const float H = static_cast<float>(h);
    const float t = static_cast<float>(seconds);

    // --- Background: deep blue to teal with a green horizon (Vista aurora) ---
    const SDL_Color top{7, 20, 38, 255};
    const SDL_Color mid{16, 68, 92, 255};
    const SDL_Color bottom{30, 108, 92, 255};
    for (int y = 0; y < h; ++y) {
        const float u = static_cast<float>(y) / std::max(1, h - 1);
        SDL_Color c;
        if (u < 0.55f) {
            const float k = u / 0.55f;
            c = {static_cast<Uint8>(top.r + (mid.r - top.r) * k),
                 static_cast<Uint8>(top.g + (mid.g - top.g) * k),
                 static_cast<Uint8>(top.b + (mid.b - top.b) * k), 255};
        } else {
            const float k = (u - 0.55f) / 0.45f;
            c = {static_cast<Uint8>(mid.r + (bottom.r - mid.r) * k),
                 static_cast<Uint8>(mid.g + (bottom.g - mid.g) * k),
                 static_cast<Uint8>(mid.b + (bottom.b - mid.b) * k), 255};
        }
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        SDL_RenderLine(r, 0, static_cast<float>(y), W, static_cast<float>(y));
    }

    // --- Aurora ribbons ---
    for (int band = 0; band < 3; ++band) {
        const float baseY = H * (0.62f + 0.08f * band);
        const float amp = H * 0.045f;
        const float speed = 0.35f + band * 0.12f;
        const float freq = (1.2f + band * 0.5f) * static_cast<float>(M_PI) * 2.f / W;
        const float thickness = H * (0.06f + 0.02f * band);
        const Uint8 alpha = static_cast<Uint8>(26 + band * 8);
        for (int x = 0; x < w; x += 6) {
            const float yy = baseY + std::sin(x * freq + t * speed + band) * amp;
            std::vector<SDL_Vertex> v{
                {{static_cast<float>(x), yy}, {0.45f, 0.95f, 0.85f, alpha / 255.f}},
                {{static_cast<float>(x + 6), yy + std::sin((x + 6) * freq + t * speed + band) * amp},
                 {0.45f, 0.95f, 0.85f, alpha / 255.f}},
                {{static_cast<float>(x + 6), yy + thickness}, {0.30f, 0.75f, 0.55f, 0.f}},
                {{static_cast<float>(x), yy + thickness}, {0.30f, 0.75f, 0.55f, 0.f}},
            };
            std::vector<int> idx{0, 1, 2, 0, 2, 3};
            geom(r, v, idx, SDL_BLENDMODE_BLEND);
        }
    }

    // --- Floating glass bubbles ---
    for (int i = 0; i < 14; ++i) {
        const float seed = static_cast<float>(i) * 1.61f;
        const float rad = 10.f + std::fmod(seed * 13.f, 34.f);
        const float cx = std::fmod(seed * 271.f, W);
        const float speed = 8.f + std::fmod(seed * 7.f, 14.f);
        const float cy = H + rad - std::fmod(t * speed + seed * 97.f, H + rad * 2.f);
        glassDisc(r, cx, cy, rad, {190, 240, 255, 60}, {120, 200, 230, 0});
        glassDisc(r, cx - rad * 0.32f, cy - rad * 0.36f, rad * 0.32f,
                  {255, 255, 255, 120}, {255, 255, 255, 0});
    }

    // --- Glass panel ---
    const float pw = std::min(W * 0.46f, 760.f);
    const float ph = std::min(H * 0.44f, 380.f);
    const SDL_FRect panel{(W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph};
    const float radius = 18.f;
    glossPanel(r, panel, radius, {16, 42, 64, 190}, {150, 210, 235, 150},
               {220, 245, 255, 40});

    // --- Spinner: glass orb with a rotating highlight ring ---
    const float scx = W * 0.5f;
    const float scy = panel.y + ph * 0.34f;
    const float sr = std::min(52.f, ph * 0.15f);
    glassDisc(r, scx, scy, sr, {60, 150, 190, 230}, {14, 50, 78, 120});
    glassDisc(r, scx - sr * 0.30f, scy - sr * 0.34f, sr * 0.46f,
              {240, 252, 255, 150}, {240, 252, 255, 0});
    const float spin = t * 2.2f;
    for (int i = 0; i < 3; ++i) {
        const float start = spin + static_cast<float>(i) * 2.f * static_cast<float>(M_PI) / 3.f;
        ringArc(r, scx, scy, sr + 7.f, start, 0.55f, 4.5f, {190, 240, 255, 210}, 20);
    }
    ringArc(r, scx, scy, sr + 7.f, spin + 0.9f, 2.4f, 2.5f, {255, 255, 255, 90}, 40);
    glassDisc(r, scx + sr * 0.28f, scy + sr * 0.30f, sr * 0.18f,
              {255, 255, 255, 90}, {255, 255, 255, 0});

    // --- Progress bar with a moving sheen ---
    const float bw = pw * 0.72f;
    const float bh = std::max(12.f, ph * 0.045f);
    const SDL_FRect track{W * 0.5f - bw * 0.5f, panel.y + ph * 0.62f, bw, bh};
    glossPanel(r, {track.x, track.y, track.w, track.h}, bh * 0.5f,
               {8, 26, 40, 220}, {120, 180, 210, 120}, {255, 255, 255, 12});
    const float p = std::clamp(progress, 0.f, 1.f);
    if (p > 0.001f) {
        const SDL_FRect fill{track.x + 2.f, track.y + 2.f,
                             (track.w - 4.f) * p, track.h - 4.f};
        glossPanel(r, fill, std::max(2.f, fill.h * 0.5f),
                   {90, 200, 235, 235}, {200, 245, 255, 200}, {255, 255, 255, 70});
        // Sheen sweeps across the filled part.
        const float sheenW = fill.w * 0.35f;
        const float sx = fill.x + std::fmod(t * fill.w * 0.9f, fill.w + sheenW) - sheenW;
        const float x0 = std::max(sx, fill.x);
        const float x1 = std::min(sx + sheenW, fill.x + fill.w);
        if (x1 > x0) {
            std::vector<SDL_Vertex> v{
                {{x0, fill.y}, {1.f, 1.f, 1.f, 0.f}},
                {{x1, fill.y}, {1.f, 1.f, 1.f, 0.35f}},
                {{x1, fill.y + fill.h}, {1.f, 1.f, 1.f, 0.10f}},
                {{x0, fill.y + fill.h}, {1.f, 1.f, 1.f, 0.f}},
            };
            std::vector<int> idx{0, 1, 2, 0, 2, 3};
            geom(r, v, idx, SDL_BLENDMODE_BLEND);
        }
    }

    // --- Text ---
    drawText(r, "Loading project", static_cast<int>(std::max(18.f, ph * 0.07f)),
             SDL_Color{235, 248, 255, 255}, W * 0.5f, panel.y + ph * 0.10f, true);
    if (!stage.empty())
        drawText(r, stage, static_cast<int>(std::max(13.f, ph * 0.045f)),
                 SDL_Color{185, 225, 240, 235}, W * 0.5f, panel.y + ph * 0.73f, true);
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(p * 100.f + 0.5f));
    drawText(r, pct, static_cast<int>(std::max(12.f, ph * 0.04f)),
             SDL_Color{220, 245, 255, 220}, W * 0.5f, panel.y + ph * 0.80f, true);
}

void renderExportBadge(SDL_Renderer* r, float x, float y, float w, float h,
                       float progress) {
    const SDL_FRect panel{x, y, w, h};
    glossPanel(r, panel, 10.f, {16, 42, 64, 200}, {150, 210, 235, 150}, {220, 245, 255, 40});

    drawText(r, "Exporting", static_cast<int>(std::max(11.f, h * 0.20f)),
             SDL_Color{235, 248, 255, 255}, x + w * 0.5f, y + h * 0.14f, true);

    const float bh = std::max(6.f, h * 0.14f);
    const SDL_FRect track{x + w * 0.08f, y + h * 0.50f, w * 0.84f, bh};
    glossPanel(r, track, bh * 0.5f, {8, 26, 40, 220}, {120, 180, 210, 120}, {255, 255, 255, 12});

    const float p = std::clamp(progress, 0.f, 1.f);
    if (p > 0.001f) {
        const SDL_FRect fill{track.x + 2.f, track.y + 2.f,
                             (track.w - 4.f) * p, track.h - 4.f};
        glossPanel(r, fill, std::max(2.f, fill.h * 0.5f),
                   {90, 200, 235, 235}, {200, 245, 255, 200}, {255, 255, 255, 70});
    }

    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(p * 100.f + 0.5f));
    drawText(r, pct, static_cast<int>(std::max(10.f, h * 0.16f)),
             SDL_Color{220, 245, 255, 220}, x + w * 0.5f, y + h * 0.70f, true);
}

} // namespace loading
