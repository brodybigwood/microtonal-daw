#include "EmbeddedWindow.h"
#include "styles.h"
#include "SDL_Events.h"
#include <algorithm>
#include <cmath>

EmbeddedWindow::EmbeddedWindow() = default;

SDL_FRect EmbeddedWindow::worldRect() const {
    return {x, y, w, h};
}

SDL_FRect EmbeddedWindow::closeButtonRect() const {
    const float btnSz = kTitleBarH - 6.f;
    return {x + w - btnSz - 6.f, y + 3.f, btnSz, btnSz};
}

// --- Polygon helpers ---

static bool pointInPolygon(const std::vector<SDL_FPoint>& poly, float px, float py) {
    if (poly.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        float xi = poly[i].x, yi = poly[i].y;
        float xj = poly[j].x, yj = poly[j].y;
        if ((yi >= py) != (yj >= py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

static void renderFilledPolygon(SDL_Renderer* r, const std::vector<SDL_FPoint>& poly) {
    if (poly.size() < 3) return;
    std::vector<SDL_Vertex> verts(poly.size());
    Uint8 cr, cg, cb, ca;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    SDL_FColor fc{cr / 255.f, cg / 255.f, cb / 255.f, ca / 255.f};
    for (size_t i = 0; i < poly.size(); ++i) {
        verts[i].position = poly[i];
        verts[i].color = fc;
    }
    std::vector<int> indices((poly.size() - 2) * 3);
    for (size_t i = 0; i < poly.size() - 2; ++i) {
        indices[i * 3 + 0] = 0;
        indices[i * 3 + 1] = static_cast<int>(i + 1);
        indices[i * 3 + 2] = static_cast<int>(i + 2);
    }
    SDL_RenderGeometry(r, nullptr, verts.data(), static_cast<int>(verts.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

static void renderPolygonOutline(SDL_Renderer* r, const std::vector<SDL_FPoint>& poly) {
    if (poly.size() < 2) return;
    for (size_t i = 0; i < poly.size(); ++i) {
        size_t j = (i + 1) % poly.size();
        SDL_RenderLine(r, poly[i].x, poly[i].y, poly[j].x, poly[j].y);
    }
}

// --- Hit testing ---

bool EmbeddedWindow::hitTest(float worldMx, float worldMy) const {
    if (!visible) return false;
    std::vector<SDL_FPoint> poly;
    buildHitPolygon(poly);
    return pointInPolygon(poly, worldMx, worldMy);
}

void EmbeddedWindow::buildHitPolygon(std::vector<SDL_FPoint>& out) const {
    out.resize(4);
    out[0] = {x, y};
    out[1] = {x + w, y};
    out[2] = {x + w, y + h};
    out[3] = {x, y + h};
}

EmbeddedWindow::ResizeZone EmbeddedWindow::getResizeZone(float mx, float my) const {
    if (!visible) return ResizeZone::None;

    std::vector<SDL_FPoint> poly;
    buildHitPolygon(poly);
    if (poly.size() < 3) return ResizeZone::None;

    const float s = kResizeHandleSz;

    // Find the closest polygon edge to the mouse point.
    float bestDist = s;
    SDL_FPoint bestEdgeA{}, bestEdgeB{};

    for (size_t i = 0; i < poly.size(); ++i) {
        size_t j = (i + 1) % poly.size();
        float ax = poly[i].x, ay = poly[i].y;
        float bx = poly[j].x, by = poly[j].y;

        float edx = bx - ax, edy = by - ay;
        float len2 = edx * edx + edy * edy;
        if (len2 < 0.0001f) continue;

        // Project mouse onto edge, clamped to [0,1].
        float t = ((mx - ax) * edx + (my - ay) * edy) / len2;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);

        float px = ax + t * edx;
        float py = ay + t * edy;
        float d2 = (mx - px) * (mx - px) + (my - py) * (my - py);
        if (d2 < bestDist * bestDist) {
            bestDist = std::sqrt(d2);
            bestEdgeA = {ax, ay};
            bestEdgeB = {bx, by};
        }
    }

    if (bestDist >= s) return ResizeZone::None;

    // Direction: vector from polygon centroid toward the mouse.
    float cx = 0.f, cy = 0.f;
    for (auto& v : poly) { cx += v.x; cy += v.y; }
    cx /= static_cast<float>(poly.size());
    cy /= static_cast<float>(poly.size());
    float dx = mx - cx;
    float dy = my - cy;

    const float adx = std::abs(dx), ady = std::abs(dy);
    if (adx > ady * 2.f)      return dx > 0 ? ResizeZone::E : ResizeZone::W;
    if (ady > adx * 2.f)      return dy > 0 ? ResizeZone::S : ResizeZone::N;
    if (dx > 0 && dy > 0)     return ResizeZone::SE;
    if (dx > 0 && dy < 0)     return ResizeZone::NE;
    if (dx < 0 && dy > 0)     return ResizeZone::SW;
    return ResizeZone::NW;
}

// --- Rendering ---

void EmbeddedWindow::renderChrome(SDL_Renderer* r) {
    std::vector<SDL_FPoint> poly;
    buildHitPolygon(poly);

    SDL_SetRenderDrawColor(r, 52, 52, 56, 255);
    renderFilledPolygon(r, poly);

    SDL_FRect tb{x, y, w, kTitleBarH};
    SDL_SetRenderDrawColor(r, 64, 64, 70, 255);
    SDL_RenderFillRect(r, &tb);

    if (fonts.mainFont && !title.empty()) {
        SDL_Surface* s = TTF_RenderText_Blended(fonts.mainFont, title.c_str(), 0, SDL_Color{220, 220, 224, 255});
        if (s) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, s);
            if (tex) {
                const float th = static_cast<float>(s->h);
                SDL_FRect tr{tb.x + 8.f, tb.y + (kTitleBarH - th) * 0.5f, static_cast<float>(s->w), th};
                SDL_RenderTexture(r, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(s);
        }
    }

    SDL_FRect cb = closeButtonRect();
    SDL_SetRenderDrawColor(r, 90, 90, 96, 255);
    SDL_RenderFillRect(r, &cb);
    SDL_SetRenderDrawColor(r, 180, 180, 184, 255);
    SDL_RenderRect(r, &cb);
    const float pad = 5.f;
    SDL_SetRenderDrawColor(r, 220, 220, 224, 255);
    SDL_RenderLine(r, cb.x + pad, cb.y + pad, cb.x + cb.w - pad, cb.y + cb.h - pad);
    SDL_RenderLine(r, cb.x + cb.w - pad, cb.y + pad, cb.x + pad, cb.y + cb.h - pad);

    SDL_SetRenderDrawColor(r, 100, 100, 108, 255);
    renderPolygonOutline(r, poly);
}

void EmbeddedWindow::render(SDL_Renderer* r) {
    if (!visible) return;
    renderChrome(r);

    SDL_Rect contentClip{
        static_cast<int>(x + kBorderW),
        static_cast<int>(y + kTitleBarH),
        static_cast<int>(w - kBorderW * 2.f),
        static_cast<int>(h - kTitleBarH - kBorderW)
    };
    if (contentClip.w > 0 && contentClip.h > 0) {
        SDL_Rect prevClip;
        SDL_GetRenderClipRect(r, &prevClip);
        SDL_Rect useClip = contentClip;
        if (!SDL_RectEmpty(&prevClip)) {
            SDL_Rect inter;
            if (SDL_GetRectIntersection(&prevClip, &contentClip, &inter))
                useClip = inter;
            else
                return;
        }
        SDL_SetRenderClipRect(r, &useClip);
        renderContent(r);
        SDL_SetRenderClipRect(r, &prevClip);
    }
}

// --- Input ---

bool EmbeddedWindow::handleInput(SDL_Event& e) {
    if (!visible) return false;

    float mx, my;
    SDL_GetMouseState(&mx, &my);

    const bool onChrome = hitTest(mx, my);

    // Close button.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_FRect cb = closeButtonRect();
        if (mx >= cb.x && mx < cb.x + cb.w && my >= cb.y && my < cb.y + cb.h && onChrome) {
            close();
            return true;
        }
    }

    // Drag (title bar).
    {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_FRect tb{x, y, w, kTitleBarH};
            if (mx >= tb.x && mx < tb.x + tb.w && my >= tb.y && my < tb.y + tb.h && onChrome) {
                dragging_ = true;
                dragOffX_ = mx - x;
                dragOffY_ = my - y;
                return true;
            }
        }
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        dragging_ = false;
        if (onChrome) return true;
    }

    if (e.type == SDL_EVENT_MOUSE_MOTION && dragging_) {
        x = mx - dragOffX_;
        y = my - dragOffY_;
        return true;
    }

    // Delegate content input if mouse is inside; always consume.
    if (onChrome && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        handleContentInput(e);
        return true;
    }

    if (onChrome)
        return true;

    return false;
}
