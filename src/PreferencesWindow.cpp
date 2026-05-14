#include "PreferencesWindow.h"
#include "styles.h"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr int kSections = 8;
static const char* kSectionNames[] = {
    "Audio", "", "", "", "", "", "", ""
};
static const char* kSectionSymbols[] = {
    "\xe2\x99\xaa", "", "", "", "", "", "", ""  // ♪ for Audio
};

PreferencesWindow::PreferencesWindow() {
    title = "Preferences";
    w = 420.f;
    h = 420.f;
}

// --- Polygon: cog/gear shape with proper inner-circle gaps ---

void PreferencesWindow::buildHitPolygon(std::vector<SDL_FPoint>& out) const {
    const float cx = centerX();
    const float cy = centerY();
    const float Ro = outerR();
    const float Ri = innerR();
    const int N = kSections;
    const int kGapSteps = 4; // sub-steps along inner circle between teeth

    const float sector = 2.f * static_cast<float>(M_PI) / static_cast<float>(N);
    const float toothHalf = sector * 0.50f; // angular half-width of tooth

    out.clear();

    for (int i = 0; i < N; ++i) {
        const float mid = static_cast<float>(i) * sector;
        const float a0 = mid - toothHalf; // tooth left edge
        const float a1 = mid + toothHalf; // tooth right edge
        const float aNext = (i == N - 1) ? (2.f * static_cast<float>(M_PI)) : (static_cast<float>(i + 1) * sector);
        const float aGapStart = a1;
        const float aGapEnd = aNext - toothHalf;

        // Tooth: inner base → outer → outer → inner base
        out.push_back({cx + Ri * cosf(a0), cy + Ri * sinf(a0)});
        out.push_back({cx + Ro * cosf(a0), cy + Ro * sinf(a0)});
        out.push_back({cx + Ro * cosf(a1), cy + Ro * sinf(a1)});
        out.push_back({cx + Ri * cosf(a1), cy + Ri * sinf(a1)});

        // Gap: follow inner circle to next tooth
        for (int s = 1; s <= kGapSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(kGapSteps + 1);
            float a = aGapStart + (aGapEnd - aGapStart) * t;
            out.push_back({cx + Ri * cosf(a), cy + Ri * sinf(a)});
        }
    }
}

// --- Tooth hit-test ---

int PreferencesWindow::hitTooth(float worldMx, float worldMy) const {
    const float cx = centerX();
    const float cy = centerY();
    const float dx = worldMx - cx;
    const float dy = worldMy - cy;
    const float dist = sqrtf(dx * dx + dy * dy);

    if (dist < toothBaseR() || dist > outerR())
        return -1;

    float angle = atan2f(dy, dx);
    if (angle < 0.f) angle += 2.f * static_cast<float>(M_PI);

    const float sector = 2.f * static_cast<float>(M_PI) / static_cast<float>(kSections);
    const float toothHalf = sector * 0.50f;

    for (int i = 0; i < kSections; ++i) {
        float mid = static_cast<float>(i) * sector;
        float d = angle - mid;
        if (d > static_cast<float>(M_PI)) d -= 2.f * static_cast<float>(M_PI);
        if (d < -static_cast<float>(M_PI)) d += 2.f * static_cast<float>(M_PI);
        if (fabsf(d) <= toothHalf)
            return i;
    }
    return -1;
}

// --- Rendering ---

void PreferencesWindow::render(SDL_Renderer* r) {
    if (!visible) return;

    const float cx = centerX();
    const float cy = centerY();
    const float Ro = outerR();
    const float Ri = innerR();
    const float Rtb = toothBaseR();
    const int N = kSections;
    const float sector = 2.f * static_cast<float>(M_PI) / static_cast<float>(N);
    const float toothHalf = sector * 0.50f;
    const int kGapSteps = 4;

    // --- Body polygon ---
    std::vector<SDL_FPoint> poly;
    for (int i = 0; i < N; ++i) {
        const float mid = static_cast<float>(i) * sector;
        const float a0 = mid - toothHalf;
        const float a1 = mid + toothHalf;
        const float aNext = (i == N - 1) ? (2.f * static_cast<float>(M_PI)) : (static_cast<float>(i + 1) * sector);
        const float aGapStart = a1;
        const float aGapEnd = aNext - toothHalf;

        poly.push_back({cx + Ri * cosf(a0), cy + Ri * sinf(a0)});
        poly.push_back({cx + Ro * cosf(a0), cy + Ro * sinf(a0)});
        poly.push_back({cx + Ro * cosf(a1), cy + Ro * sinf(a1)});
        poly.push_back({cx + Ri * cosf(a1), cy + Ri * sinf(a1)});

        for (int s = 1; s <= kGapSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(kGapSteps + 1);
            float a = aGapStart + (aGapEnd - aGapStart) * t;
            poly.push_back({cx + Ri * cosf(a), cy + Ri * sinf(a)});
        }
    }

    // Fill body.
    {
        std::vector<SDL_Vertex> verts(poly.size());
        SDL_FColor fc{0.20f, 0.20f, 0.22f, 1.f};
        for (size_t i = 0; i < poly.size(); ++i) {
            verts[i].position = poly[i];
            verts[i].color = fc;
        }
        std::vector<int> idx((poly.size() - 2) * 3);
        for (size_t i = 0; i < poly.size() - 2; ++i) {
            idx[i * 3 + 0] = 0;
            idx[i * 3 + 1] = static_cast<int>(i + 1);
            idx[i * 3 + 2] = static_cast<int>(i + 2);
        }
        SDL_RenderGeometry(r, nullptr, verts.data(), static_cast<int>(verts.size()),
                           idx.data(), static_cast<int>(idx.size()));
    }

    // Body outline.
    SDL_SetRenderDrawColor(r, 90, 90, 96, 255);
    for (size_t i = 0; i < poly.size(); ++i) {
        size_t j = (i + 1) % poly.size();
        SDL_RenderLine(r, poly[i].x, poly[i].y, poly[j].x, poly[j].y);
    }

    // --- Tooth buttons ---
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    const int hoveredTooth = hitTooth(mx, my);

    for (int i = 0; i < N; ++i) {
        const float mid = static_cast<float>(i) * sector;
        const float a0 = mid - toothHalf;
        const float a1 = mid + toothHalf;

        const bool isActive = (i == activeSection_);
        const bool hasSection = (kSectionNames[i][0] != '\0');
        const bool hovered = (i == hoveredTooth);

        // Tooth button fill.
        if (isActive) {
            SDL_SetRenderDrawColor(r, 80, 130, 200, 255);
        } else if (hovered && hasSection) {
            SDL_SetRenderDrawColor(r, 65, 65, 72, 255);
        } else if (hasSection) {
            SDL_SetRenderDrawColor(r, 55, 55, 60, 255);
        } else {
            SDL_SetRenderDrawColor(r, 42, 42, 46, 255);
        }

        // Render tooth button as a quad fan from toothBaseR to outerR.
        {
            std::vector<SDL_Vertex> verts(4);
            SDL_FColor col;
            if (isActive) col = {80.f/255.f, 130.f/255.f, 200.f/255.f, 1.f};
            else if (hovered && hasSection) col = {0.255f, 0.255f, 0.282f, 1.f};
            else if (hasSection) col = {0.216f, 0.216f, 0.235f, 1.f};
            else col = {0.165f, 0.165f, 0.180f, 1.f};

            verts[0].position = {cx + Rtb * cosf(a0), cy + Rtb * sinf(a0)};
            verts[0].color = col;
            verts[1].position = {cx + Ro * cosf(a0), cy + Ro * sinf(a0)};
            verts[1].color = col;
            verts[2].position = {cx + Ro * cosf(a1), cy + Ro * sinf(a1)};
            verts[2].color = col;
            verts[3].position = {cx + Rtb * cosf(a1), cy + Rtb * sinf(a1)};
            verts[3].color = col;

            int indices[] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(r, nullptr, verts.data(), 4, indices, 6);
        }

        // Tooth button border.
        SDL_SetRenderDrawColor(r, 120, 120, 130, 255);
        SDL_FPoint tb[] = {
            {cx + Rtb * cosf(a0), cy + Rtb * sinf(a0)},
            {cx + Ro * cosf(a0),  cy + Ro * sinf(a0)},
            {cx + Ro * cosf(a1),  cy + Ro * sinf(a1)},
            {cx + Rtb * cosf(a1), cy + Rtb * sinf(a1)},
        };
        SDL_RenderLine(r, tb[0].x, tb[0].y, tb[1].x, tb[1].y);
        SDL_RenderLine(r, tb[1].x, tb[1].y, tb[2].x, tb[2].y);
        SDL_RenderLine(r, tb[2].x, tb[2].y, tb[3].x, tb[3].y);
        SDL_RenderLine(r, tb[3].x, tb[3].y, tb[0].x, tb[0].y);

        // Symbol (always shown if has section).
        if (hasSection && fonts.mainFont && kSectionSymbols[i][0] != '\0') {
            SDL_Surface* s = TTF_RenderText_Blended(fonts.mainFont, kSectionSymbols[i], 0, SDL_Color{220, 220, 230, 255});
            if (s) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, s);
                if (tex) {
                    const float Rmid = (Ro + Rtb) * 0.5f;
                    const float lx = cx + Rmid * cosf(mid) - static_cast<float>(s->w) * 0.5f;
                    const float ly = cy + Rmid * sinf(mid) - static_cast<float>(s->h) * 0.5f;
                    SDL_FRect tr{lx, ly, static_cast<float>(s->w), static_cast<float>(s->h)};
                    SDL_RenderTexture(r, tex, nullptr, &tr);
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(s);
            }
        }

        // Hover text label (shown only when hovering, using shared tooltip style).
        if (hovered && hasSection) {
            const float tipX = cx + (Ro + 4.f) * cosf(mid);
            const float tipY = cy + (Ro + 4.f) * sinf(mid);
            renderTooltip(r, kSectionNames[i], tipX, tipY, worldRect());
        }
    }

    // --- Inner circle (content area) ---
    const int circleSegs = 56;
    {
        std::vector<SDL_Vertex> verts(static_cast<size_t>(circleSegs) + 2);
        verts[0].position = {cx, cy};
        verts[0].color = {0.17f, 0.17f, 0.19f, 1.f};
        for (int i = 0; i <= circleSegs; ++i) {
            float a = static_cast<float>(i) * 2.f * static_cast<float>(M_PI) / static_cast<float>(circleSegs);
            verts[static_cast<size_t>(i) + 1].position = {cx + Ri * cosf(a), cy + Ri * sinf(a)};
            verts[static_cast<size_t>(i) + 1].color = {0.17f, 0.17f, 0.19f, 1.f};
        }
        std::vector<int> idx(static_cast<size_t>(circleSegs) * 3);
        for (int i = 0; i < circleSegs; ++i) {
            idx[static_cast<size_t>(i) * 3 + 0] = 0;
            idx[static_cast<size_t>(i) * 3 + 1] = i + 1;
            idx[static_cast<size_t>(i) * 3 + 2] = i + 2;
        }
        SDL_RenderGeometry(r, nullptr, verts.data(), static_cast<int>(verts.size()),
                           idx.data(), static_cast<int>(idx.size()));
    }

    // Inner circle border.
    SDL_SetRenderDrawColor(r, 80, 80, 88, 255);
    for (int i = 0; i < circleSegs; ++i) {
        float a0 = static_cast<float>(i) * 2.f * static_cast<float>(M_PI) / static_cast<float>(circleSegs);
        float a1 = static_cast<float>(i + 1) * 2.f * static_cast<float>(M_PI) / static_cast<float>(circleSegs);
        SDL_RenderLine(r, cx + Ri * cosf(a0), cy + Ri * sinf(a0),
                       cx + Ri * cosf(a1), cy + Ri * sinf(a1));
    }

    // --- Center content ---
    if (fonts.mainFont) {
        const char* title = (activeSection_ == 0) ? "Audio Settings" : "Preferences";
        SDL_Surface* st = TTF_RenderText_Blended(fonts.mainFont, title, 0, SDL_Color{230, 230, 240, 255});
        if (st) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, st);
            if (tex) {
                SDL_FRect tr{cx - static_cast<float>(st->w) * 0.5f, cy - static_cast<float>(st->h) - 6.f,
                             static_cast<float>(st->w), static_cast<float>(st->h)};
                SDL_RenderTexture(r, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(st);
        }

        const char* sub = (activeSection_ == 0) ? "settings coming soon" : "select a section";
        SDL_Surface* sp = TTF_RenderText_Blended(fonts.mainFont, sub, 0, SDL_Color{120, 120, 130, 255});
        if (sp) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, sp);
            if (tex) {
                SDL_FRect tr{cx - static_cast<float>(sp->w) * 0.5f, cy + 10.f,
                             static_cast<float>(sp->w), static_cast<float>(sp->h)};
                SDL_RenderTexture(r, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(sp);
        }
    }

    // --- Close button (small X in top-right of inner circle) ---
    {
        const float bx = cx + Ri * 0.55f;
        const float by = cy - Ri * 0.55f;
        const float bs = 14.f;
        SDL_FRect cb{bx - bs * 0.5f, by - bs * 0.5f, bs, bs};
        SDL_SetRenderDrawColor(r, 80, 80, 88, 255);
        SDL_RenderFillRect(r, &cb);
        SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
        SDL_RenderLine(r, cb.x + 3.f, cb.y + 3.f, cb.x + cb.w - 3.f, cb.y + cb.h - 3.f);
        SDL_RenderLine(r, cb.x + cb.w - 3.f, cb.y + 3.f, cb.x + 3.f, cb.y + cb.h - 3.f);
    }
}

// --- Input ---

bool PreferencesWindow::handleInput(SDL_Event& e) {
    if (!visible) return false;

    float mx, my;
    SDL_GetMouseState(&mx, &my);

    // Point-in-polygon on cog.
    std::vector<SDL_FPoint> poly;
    buildHitPolygon(poly);
    bool onCog = false;
    {
        size_t n = poly.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            float xi = poly[i].x, yi = poly[i].y;
            float xj = poly[j].x, yj = poly[j].y;
            if ((yi > my) != (yj > my) && mx < (xj - xi) * (my - yi) / (yj - yi) + xi)
                onCog = !onCog;
        }
    }

    if (!onCog) return false;

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        // Close button?
        const float cx = centerX();
        const float cy = centerY();
        const float Ri = innerR();
        const float bx = cx + Ri * 0.55f;
        const float by = cy - Ri * 0.55f;
        const float bs = 14.f;
        if (mx >= bx - bs * 0.5f && mx < bx + bs * 0.5f &&
            my >= by - bs * 0.5f && my < by + bs * 0.5f) {
            close();
            return true;
        }

        // Tooth click?
        int tooth = hitTooth(mx, my);
        if (tooth >= 0) {
            // Only Audio (index 0) is active.
            if (tooth == 0)
                activeSection_ = tooth;
            return true;
        }

        // Start drag from anywhere on the cog.
        dragging_ = true;
        dragOffX_ = mx - x;
        dragOffY_ = my - y;
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        dragging_ = false;
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_MOTION && dragging_) {
        x = mx - dragOffX_;
        y = my - dragOffY_;
        return true;
    }

    return true;
}
