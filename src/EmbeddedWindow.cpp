#include "EmbeddedWindow.h"
#include "styles.h"
#include "SDL_Events.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
    if (!polygonDirty_) {
        out = cachedPolygon_;
        return;
    }
    out.resize(4);
    out[0] = {x, y};
    out[1] = {x + w, y};
    out[2] = {x + w, y + h};
    out[3] = {x, y + h};
    cachedPolygon_ = out;
    polygonDirty_ = false;
}

EmbeddedWindow::ResizeZone EmbeddedWindow::getResizeZone(float mx, float my) const {
    if (!visible) return ResizeZone::None;

    std::vector<SDL_FPoint> poly;
    buildHitPolygon(poly);
    if (poly.size() < 3) return ResizeZone::None;

    const float s = kResizeHandleSz;

    // Find the two closest polygon edges to the mouse point.
    float bestDist = s, secondDist = s;
    size_t bestI = 0, secondI = 0;

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
        float d = std::sqrt((mx - px) * (mx - px) + (my - py) * (my - py));
        if (d < bestDist) {
            secondDist = bestDist;
            secondI = bestI;
            bestDist = d;
            bestI = i;
        } else if (d < secondDist) {
            secondDist = d;
            secondI = i;
        }
    }

    if (bestDist >= s) return ResizeZone::None;

    if (hasRectResize()) {
        // Edge normal direction for rectangular windows.
        size_t bestJ = (bestI + 1) % poly.size();
        float edx = poly[bestJ].x - poly[bestI].x;
        float edy = poly[bestJ].y - poly[bestI].y;
        float rnx = edy, rny = -edx;  // outward normal for clockwise polygon

        // If a second edge is also within threshold, combine normals (corner).
        if (secondDist < s) {
            size_t sj = (secondI + 1) % poly.size();
            float sdx = poly[sj].x - poly[secondI].x;
            float sdy = poly[sj].y - poly[secondI].y;
            rnx += sdy;
            rny += -sdx;
            return (rnx > 0) ? ((rny > 0) ? ResizeZone::SE : ResizeZone::NE)
                            : ((rny > 0) ? ResizeZone::SW : ResizeZone::NW);
        }

        // Map normal to cardinal direction.
        if (std::abs(rnx) > std::abs(rny)) return rnx > 0 ? ResizeZone::E : ResizeZone::W;
        return rny > 0 ? ResizeZone::S : ResizeZone::N;
    }

    // Direction from polygon centroid for complex shapes.
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
        const bool hadClip = !SDL_RectEmpty(&prevClip);
        SDL_Rect useClip = contentClip;
        if (hadClip) {
            SDL_Rect inter;
            if (SDL_GetRectIntersection(&prevClip, &contentClip, &inter))
                useClip = inter;
            else
                return;
        }
        SDL_SetRenderClipRect(r, &useClip);
        renderContent(r);
        SDL_SetRenderClipRect(r, hadClip ? &prevClip : nullptr);
    }
}

// --- Resize ---

bool EmbeddedWindow::startResize(float worldMx, float worldMy) {
    resizeZone_ = getResizeZone(worldMx, worldMy);
    if (resizeZone_ == ResizeZone::None) return false;
    resizing_ = true;
    resizeStartMouseX_ = worldMx;
    resizeStartMouseY_ = worldMy;
    resizeStartX_ = x;
    resizeStartY_ = y;
    resizeStartW_ = w;
    resizeStartH_ = h;
    resizeStartZoom_ = zoom_;
    return true;
}

void EmbeddedWindow::applyResizeDelta(float dx, float dy) {
    float nx = resizeStartX_, ny = resizeStartY_, nw = resizeStartW_, nh = resizeStartH_;

    if (hasRectResize()) {
        // Free per-axis resize for rectangular windows.
        switch (resizeZone_) {
            case ResizeZone::N:
                nh = resizeStartH_ - dy;
                ny = resizeStartY_ + dy;
                if (nh < minH()) { ny -= minH() - nh; nh = minH(); }
                if (nw < minW()) nw = minW();
                break;
            case ResizeZone::S:
                nh = resizeStartH_ + dy;
                if (nh < minH()) nh = minH();
                if (nw < minW()) nw = minW();
                break;
            case ResizeZone::E:
                nw = resizeStartW_ + dx;
                if (nw < minW()) nw = minW();
                if (nh < minH()) nh = minH();
                break;
            case ResizeZone::W:
                nw = resizeStartW_ - dx;
                nx = resizeStartX_ + dx;
                if (nw < minW()) { nx -= minW() - nw; nw = minW(); }
                if (nh < minH()) nh = minH();
                break;
            case ResizeZone::NE: {
                // Uniform scale anchored at bottom-left corner, top-right tracks mouse.
                float ax = resizeStartX_, ay = resizeStartY_ + resizeStartH_;
                float mx = resizeStartMouseX_ + dx, my = resizeStartMouseY_ + dy;
                float trgW = mx - ax, trgH = ay - my;
                float s = std::max(trgW / resizeStartW_, trgH / resizeStartH_);
                nw = resizeStartW_ * s;
                nh = resizeStartH_ * s;
                ny = ay - nh;
                if (nw < minW()) nw = minW();
                if (nh < minH()) { ny -= minH() - nh; nh = minH(); }
                zoom_ = resizeStartZoom_ * (nw / resizeStartW_);
                break;
            }
            case ResizeZone::NW: {
                // Uniform scale anchored at bottom-right corner, top-left tracks mouse.
                float ax = resizeStartX_ + resizeStartW_, ay = resizeStartY_ + resizeStartH_;
                float mx = resizeStartMouseX_ + dx, my = resizeStartMouseY_ + dy;
                float trgW = ax - mx, trgH = ay - my;
                float s = std::max(trgW / resizeStartW_, trgH / resizeStartH_);
                nw = resizeStartW_ * s;
                nh = resizeStartH_ * s;
                nx = ax - nw;
                ny = ay - nh;
                if (nw < minW()) { nx -= minW() - nw; nw = minW(); }
                if (nh < minH()) { ny -= minH() - nh; nh = minH(); }
                zoom_ = resizeStartZoom_ * (nw / resizeStartW_);
                break;
            }
            case ResizeZone::SE:
                nw = resizeStartW_ + dx;
                nh = resizeStartH_ + dy;
                if (nw < minW()) nw = minW();
                if (nh < minH()) nh = minH();
                break;
            case ResizeZone::SW:
                nw = resizeStartW_ - dx;
                nx = resizeStartX_ + dx;
                nh = resizeStartH_ + dy;
                if (nw < minW()) { nx -= minW() - nw; nw = minW(); }
                if (nh < minH()) nh = minH();
                break;
            default: return;
        }
    } else {
        // Uniform resize for complex/circular shapes.
        switch (resizeZone_) {
            case ResizeZone::N:
                nh = resizeStartH_ - dy;
                ny = resizeStartY_ + dy;
                nw = resizeStartW_ - dy;           // keep uniform
                nx = resizeStartX_ + dy * 0.5f;    // keep center x stable
                if (nh < minH()) { float adj = minH() - nh; nh = minH(); nw += adj; nx -= adj * 0.5f; ny -= adj; }
                if (nw < minW()) { float adj = minW() - nw; nw = minW(); nh += adj; ny -= adj * 0.5f; nx += adj * 0.5f; }
                break;
            case ResizeZone::S:
                nh = resizeStartH_ + dy;
                nw = resizeStartW_ + dy;            // keep uniform
                nx = resizeStartX_ - dy * 0.5f;     // keep center x stable
                if (nh < minH()) { float adj = minH() - nh; nh = minH(); nw += adj; nx -= adj * 0.5f; }
                if (nw < minW()) { float adj = minW() - nw; nw = minW(); nh += adj; ny -= adj * 0.5f; nx += adj * 0.5f; }
                break;
            case ResizeZone::E:
                nw = resizeStartW_ + dx;
                nh = resizeStartH_ + dx;            // keep uniform
                ny = resizeStartY_ - dx * 0.5f;     // keep center y stable
                if (nw < minW()) { float adj = minW() - nw; nw = minW(); nh += adj; ny -= adj * 0.5f; }
                if (nh < minH()) { float adj = minH() - nh; nh = minH(); nw += adj; nx -= adj * 0.5f; ny += adj * 0.5f; }
                break;
            case ResizeZone::W:
                nw = resizeStartW_ - dx;
                nx = resizeStartX_ + dx;
                nh = resizeStartH_ - dx;            // keep uniform
                ny = resizeStartY_ + dx * 0.5f;     // keep center y stable
                if (nw < minW()) { float adj = minW() - nw; nw = minW(); nh += adj; ny -= adj * 0.5f; nx -= adj; }
                if (nh < minH()) { float adj = minH() - nh; nh = minH(); nw += adj; nx -= adj * 0.5f; ny += adj * 0.5f; }
                break;
            case ResizeZone::NE: {
                float d = (dx > -dy) ? dx : -dy;
                nw = resizeStartW_ + d;
                nx = resizeStartX_;
                nh = resizeStartH_ + d;
                ny = resizeStartY_ - d;
                if (nw < minW()) { float adj = minW() - nw; nw = minW(); nh += adj; ny -= adj * 0.5f; }
                if (nh < minH()) { float adj = minH() - nh; nh = minH(); nw += adj; nx -= adj * 0.5f; ny += adj; }
                break;
            }
            case ResizeZone::NW: {
                float d = (-dx > -dy) ? -dx : -dy;
                nw = resizeStartW_ + d;
                nx = resizeStartX_ - d;
                nh = resizeStartH_ + d;
                ny = resizeStartY_ - d;
                if (nw < minW()) { float adj = minW() - nw; nw = minW(); nh += adj; ny -= adj * 0.5f; nx -= adj; }
                if (nh < minH()) { float adj = minH() - nh; nh = minH(); nw += adj; nx -= adj * 0.5f; ny += adj; }
                break;
            }
            case ResizeZone::SE: {
                float d = (dx > dy) ? dx : dy;
                nw = resizeStartW_ + d;
                nx = resizeStartX_;
                nh = resizeStartH_ + d;
                ny = resizeStartY_;
                if (nw < minW()) { float adj = minW() - nw; nw = minW(); nh += adj; ny -= adj * 0.5f; }
                if (nh < minH()) { float adj = minH() - nh; nh = minH(); nw += adj; nx -= adj * 0.5f; }
                break;
            }
            case ResizeZone::SW: {
                float d = (-dx > dy) ? -dx : dy;
                nw = resizeStartW_ + d;
                nx = resizeStartX_ - d;
                nh = resizeStartH_ + d;
                ny = resizeStartY_;
                if (nw < minW()) { float adj = minW() - nw; nw = minW(); nh += adj; ny -= adj * 0.5f; nx -= adj; }
                if (nh < minH()) { float adj = minH() - nh; nh = minH(); nw += adj; nx -= adj * 0.5f; }
                break;
            }
            default: return;
        }
    }
    x = nx; y = ny; w = nw; h = nh;
    markPolygonDirty();
}

bool EmbeddedWindow::handleResizeInput(SDL_Event& e, float mx, float my, bool shiftHeld) {
    if (resizing_) {
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            applyResizeDelta(mx - resizeStartMouseX_, my - resizeStartMouseY_);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            resizing_ = false;
            resizeZone_ = ResizeZone::None;
            return true;
        }
        return true;
    }
    // Shift+drag motion/up — only when already dragging.
    if (dragging_ && shiftHeld) {
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            moveTo(mx - dragOffX_, my - dragOffY_);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            dragging_ = false;
            return true;
        }
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (shiftHeld && getResizeZone(mx, my) != ResizeZone::None) {
            dragging_ = true;
            dragOffX_ = mx - x;
            dragOffY_ = my - y;
            return true;
        }
        if (startResize(mx, my)) return true;
    }
    return false;
}

// --- Input ---

bool EmbeddedWindow::handleInput(SDL_Event& e) {
    if (!visible) return false;

    float mx, my;
    SDL_GetMouseState(&mx, &my);

    // Close button (rect windows only).
    if (hasRectResize() && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_FRect cb = closeButtonRect();
        if (mx >= cb.x && mx < cb.x + cb.w && my >= cb.y && my < cb.y + cb.h) {
            close();
            return true;
        }
    }

    return handleContentInput(e);
}
