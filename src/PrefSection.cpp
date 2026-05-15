#include "PrefSection.h"
#include "styles.h"
#include <cmath>
#include <algorithm>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- Symbol drawing ---

static void drawFilledCircle(SDL_Renderer* r, float cx, float cy, float radius, int segs) {
    std::vector<SDL_Vertex> verts(static_cast<size_t>(segs) + 2);
    SDL_FColor white{0.90f, 0.90f, 0.94f, 1.f};
    verts[0].position = {cx, cy};
    verts[0].color = white;
    for (int i = 0; i <= segs; ++i) {
        float a = static_cast<float>(i) * 2.f * static_cast<float>(M_PI) / static_cast<float>(segs);
        verts[static_cast<size_t>(i) + 1].position = {cx + radius * std::cos(a), cy + radius * std::sin(a)};
        verts[static_cast<size_t>(i) + 1].color = white;
    }
    std::vector<int> idx(static_cast<size_t>(segs) * 3);
    for (int i = 0; i < segs; ++i) {
        idx[static_cast<size_t>(i) * 3 + 0] = 0;
        idx[static_cast<size_t>(i) * 3 + 1] = i + 1;
        idx[static_cast<size_t>(i) * 3 + 2] = i + 2;
    }
    SDL_RenderGeometry(r, nullptr, verts.data(), static_cast<int>(verts.size()),
                       idx.data(), static_cast<int>(idx.size()));
}

void AudioSection::drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const {
    // Music note: filled circle (head) + vertical line (stem) + angled flag
    const float rad = sz * 0.2f;
    const float headY = cy + sz * 0.3f;
    const float stemX = cx + rad * 0.85f;
    const float stemTop = cy - sz * 0.35f;

    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);

    // Note head
    drawFilledCircle(r, cx, headY, rad, 14);

    // Stem
    SDL_RenderLine(r, stemX, headY, stemX, stemTop);

    // Flag
    const float fx = stemX + sz * 0.28f;
    SDL_RenderLine(r, stemX, stemTop, fx, stemTop + sz * 0.16f);
    SDL_RenderLine(r, stemX, stemTop + sz * 0.08f, fx, stemTop + sz * 0.22f);
}

void GUISection::drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const {
    // Window: rectangle with title bar and bottom bar (status)
    const float hw = sz * 0.36f, hh = sz * 0.30f;
    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);

    SDL_FRect frame{cx - hw, cy - hh, hw * 2.f, hh * 2.f};
    SDL_RenderRect(r, &frame);

    // Title bar
    const float tbY = cy - hh + sz * 0.11f;
    SDL_RenderLine(r, cx - hw, tbY, cx + hw, tbY);

    // Bottom bar
    const float bbY = cy + hh - sz * 0.09f;
    SDL_RenderLine(r, cx - hw, bbY, cx + hw, bbY);

    // Center cross (content placeholder)
    const float ci = sz * 0.08f;
    SDL_RenderLine(r, cx - ci, cy, cx + ci, cy);
    SDL_RenderLine(r, cx, cy - ci, cx, cy + ci);
}

void ControlsSection::drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const {
    // Sliders/knobs: rectangle with 3 vertical lines (sliders)
    const float hw = sz * 0.32f, hh = sz * 0.28f;
    SDL_SetRenderDrawColor(r, 230, 230, 240, 255);

    SDL_FRect frame{cx - hw, cy - hh, hw * 2.f, hh * 2.f};
    SDL_RenderRect(r, &frame);

    // Three vertical slider lines with small circles (knobs)
    for (int k = -1; k <= 1; ++k) {
        const float sx = cx + static_cast<float>(k) * sz * 0.17f;
        const float sTop = cy - hh + sz * 0.06f;
        const float sBot = cy + hh - sz * 0.06f;
        SDL_RenderLine(r, sx, sTop, sx, sBot);

        // Knob position (circle)
        const float knobY = cy + static_cast<float>(k) * sz * 0.06f;
        drawFilledCircle(r, sx, knobY, sz * 0.05f, 8);
    }
}

// --- Title rendering with word wrapping ---

static void renderTextCentered(SDL_Renderer* r, const char* t, float cx, float y, float s,
                               const SDL_Color& col) {
    if (!fonts.mainFont) return;
    SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, t, 0, col);
    if (!sf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, sf);
    if (tex) {
        SDL_FRect tr{cx - static_cast<float>(sf->w) * s * 0.5f, y,
                     static_cast<float>(sf->w) * s, static_cast<float>(sf->h) * s};
        SDL_RenderTexture(r, tex, nullptr, &tr);
        SDL_DestroyTexture(tex);
    }
    SDL_DestroySurface(sf);
}

static void renderSectionTitle(SDL_Renderer* r, const char* t, const SDL_FRect& b, float s) {
    if (!fonts.mainFont) return;

    const float cx = b.x + b.w * 0.5f;
    const float Ri = b.w * 0.5f;
    const float ty = b.y + 16.f * s;
    // Chord width at distance (ty - b.y) from top of circle.
    const float dy = ty - b.y;
    const float halfChord = std::sqrt(std::max(0.f, Ri * Ri - (Ri - dy) * (Ri - dy)));
    const float usableW = 2.f * halfChord - 12.f * s; // small margin
    SDL_Color col{230, 230, 240, 255};

    // Measure full text.
    SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, t, 0, col);
    if (!sf) return;
    const float fullW = static_cast<float>(sf->w) * s;
    SDL_DestroySurface(sf);

    if (fullW <= usableW) {
        renderTextCentered(r, t, cx, ty, s, col);
        return;
    }

    // Too wide — find a space to split on.
    std::string text(t);
    size_t sp = text.find(' ');
    if (sp == std::string::npos) {
        renderTextCentered(r, t, cx, ty, s, col);
        return;
    }

    std::string l1 = text.substr(0, sp);
    std::string l2 = text.substr(sp + 1);

    SDL_Surface* s1 = TTF_RenderText_Blended(fonts.mainFont, l1.c_str(), 0, col);
    if (s1) {
        renderTextCentered(r, l1.c_str(), cx, ty, s, col);
        float lineH = static_cast<float>(s1->h) * s;
        renderTextCentered(r, l2.c_str(), cx, ty + lineH + 3.f * s, s, col);
        SDL_DestroySurface(s1);
    }
}

void AudioSection::renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) {
    renderSectionTitle(r, "Audio Settings", b, s);
}

void GUISection::renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) {
    renderSectionTitle(r, "GUI Settings", b, s);
}

void ControlsSection::renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) {
    renderSectionTitle(r, "Controls Settings", b, s);
}
