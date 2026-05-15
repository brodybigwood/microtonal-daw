#include "PreferencesWindow.h"
#include "styles.h"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PreferencesWindow::PreferencesWindow() {
    title = "Preferences";
    w = 420.f;
    h = 420.f;
    initSections();
}

void PreferencesWindow::initSections() {
    sections_ = {};
    sections_[0] = &audio_;
    sections_[1] = &gui_;
    sections_[2] = &controls_;
}

// --- Polygon: cog/gear shape with proper inner-circle gaps ---

void PreferencesWindow::buildHitPolygon(std::vector<SDL_FPoint>& out) const {
    if (!polygonDirty_) {
        out = cachedPolygon_;
        return;
    }
    const float cx = centerX();
    const float cy = centerY();
    const float Ro = outerR();
    const float Ri = innerR();
    const int N = kTeeth;
    const int kArcSteps = 4; // sub-steps along outer/inner circle arcs

    const float sector = 2.f * static_cast<float>(M_PI) / static_cast<float>(N);
    const float toothHalf = sector * 0.35f; // angular half-width of tooth

    out.clear();

    for (int i = 0; i < N; ++i) {
        const float mid = static_cast<float>(i) * sector;
        const float a0 = mid - toothHalf; // tooth left edge
        const float a1 = mid + toothHalf; // tooth right edge
        const float aNext = (i == N - 1) ? (2.f * static_cast<float>(M_PI)) : (static_cast<float>(i + 1) * sector);
        const float aGapStart = a1;
        const float aGapEnd = aNext - toothHalf;

        // Tooth: inner base → outer left → outer arc → outer right → inner base
        out.push_back({cx + Ri * std::cos(a0), cy + Ri * std::sin(a0)});
        out.push_back({cx + Ro * std::cos(a0), cy + Ro * std::sin(a0)});
        for (int s = 1; s <= kArcSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(kArcSteps + 1);
            float a = a0 + (a1 - a0) * t;
            out.push_back({cx + Ro * std::cos(a), cy + Ro * std::sin(a)});
        }
        out.push_back({cx + Ro * std::cos(a1), cy + Ro * std::sin(a1)});
        out.push_back({cx + Ri * std::cos(a1), cy + Ri * std::sin(a1)});

        // Gap: follow inner circle to next tooth
        for (int s = 1; s <= kArcSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(kArcSteps + 1);
            float a = aGapStart + (aGapEnd - aGapStart) * t;
            out.push_back({cx + Ri * std::cos(a), cy + Ri * std::sin(a)});
        }
    }
    cachedPolygon_ = out;
    polygonDirty_ = false;
}

// --- Tooth hit-test ---

int PreferencesWindow::hitTooth(float worldMx, float worldMy) const {
    const float cx = centerX();
    const float cy = centerY();
    const float dx = worldMx - cx;
    const float dy = worldMy - cy;
    const float Ro = outerR();
    const float Rtb = toothBaseR();

    const float sector = 2.f * static_cast<float>(M_PI) / static_cast<float>(kTeeth);
    const float toothHalf = sector * 0.35f;

    for (int i = 0; i < kTeeth; ++i) {
        float mid = static_cast<float>(i) * sector;

        // Project onto tooth's radial direction; compare against chord (matches visual quad).
        const float proj = dx * std::cos(mid) + dy * std::sin(mid);
        if (proj < Rtb * std::cos(toothHalf) || proj > Ro * std::cos(toothHalf))
            continue;

        float angle = std::atan2(dy, dx);
        if (angle < 0.f) angle += 2.f * static_cast<float>(M_PI);
        float d = angle - mid;
        if (d > static_cast<float>(M_PI)) d -= 2.f * static_cast<float>(M_PI);
        if (d < -static_cast<float>(M_PI)) d += 2.f * static_cast<float>(M_PI);
        if (std::abs(d) <= toothHalf)
            return i;
    }
    return -1;
}

// --- Rendering ---

void PreferencesWindow::render(SDL_Renderer* r) {
    if (!visible) return;

    const float scale = std::min(w, h) / 420.f; // default size 420; controls text/close-button scale
    const float cx = centerX();
    const float cy = centerY();
    const float Ro = outerR();
    const float Ri = innerR();
    const float Rtb = toothBaseR();
    const int N = kTeeth;
    const float sector = 2.f * static_cast<float>(M_PI) / static_cast<float>(N);
    const float toothHalf = sector * 0.35f;
    const int kArcSteps = 4;

    // --- Body polygon ---
    std::vector<SDL_FPoint> poly;
    buildHitPolygon(poly);

    // Fill body — triangle fan from center (cog is star-shaped).
    {
        std::vector<SDL_Vertex> verts(poly.size() + 1);
        SDL_FColor fc{0.20f, 0.20f, 0.22f, 1.f};
        verts[0].position = {cx, cy};
        verts[0].color = fc;
        for (size_t i = 0; i < poly.size(); ++i) {
            verts[i + 1].position = poly[i];
            verts[i + 1].color = fc;
        }
        std::vector<int> idx(poly.size() * 3);
        for (size_t i = 0; i < poly.size(); ++i) {
            idx[i * 3 + 0] = 0;
            idx[i * 3 + 1] = static_cast<int>(i + 1);
            idx[i * 3 + 2] = static_cast<int>((i + 1) % poly.size() + 1);
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

        PrefSection* sec = sections_[i];
        const bool isActive = (i == activeSection_);
        const bool hasSection = (sec != nullptr);
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

        // Tooth button polygon with outer arc matching body shape.
        {
            const int nv = 4 + kArcSteps; // inner-L, outer-L, arc..., outer-R, inner-R
            std::vector<SDL_Vertex> verts(static_cast<size_t>(nv));
            SDL_FColor col;
            if (isActive) col = {80.f/255.f, 130.f/255.f, 200.f/255.f, 1.f};
            else if (hovered && hasSection) col = {0.255f, 0.255f, 0.282f, 1.f};
            else if (hasSection) col = {0.216f, 0.216f, 0.235f, 1.f};
            else col = {0.165f, 0.165f, 0.180f, 1.f};

            verts[0].position = {cx + Rtb * std::cos(a0), cy + Rtb * std::sin(a0)};
            verts[0].color = col;
            verts[1].position = {cx + Ro * std::cos(a0), cy + Ro * std::sin(a0)};
            verts[1].color = col;
            for (int s = 1; s <= kArcSteps; ++s) {
                float t = static_cast<float>(s) / static_cast<float>(kArcSteps + 1);
                float a = a0 + (a1 - a0) * t;
                verts[1 + s].position = {cx + Ro * std::cos(a), cy + Ro * std::sin(a)};
                verts[1 + s].color = col;
            }
            verts[2 + kArcSteps].position = {cx + Ro * std::cos(a1), cy + Ro * std::sin(a1)};
            verts[2 + kArcSteps].color = col;
            verts[3 + kArcSteps].position = {cx + Rtb * std::cos(a1), cy + Rtb * std::sin(a1)};
            verts[3 + kArcSteps].color = col;

            std::vector<int> indices(static_cast<size_t>(nv - 2) * 3);
            for (int j = 0; j < nv - 2; ++j) {
                indices[j * 3 + 0] = 0;
                indices[j * 3 + 1] = j + 1;
                indices[j * 3 + 2] = j + 2;
            }
            SDL_RenderGeometry(r, nullptr, verts.data(), nv,
                               indices.data(), static_cast<int>(indices.size()));
        }

        // Tooth button border with outer arc.
        SDL_SetRenderDrawColor(r, 120, 120, 130, 255);
        {
            const int nb = 4 + kArcSteps;
            std::vector<SDL_FPoint> border(static_cast<size_t>(nb));
            border[0] = {cx + Rtb * std::cos(a0), cy + Rtb * std::sin(a0)};
            border[1] = {cx + Ro * std::cos(a0),  cy + Ro * std::sin(a0)};
            for (int s = 1; s <= kArcSteps; ++s) {
                float t = static_cast<float>(s) / static_cast<float>(kArcSteps + 1);
                float a = a0 + (a1 - a0) * t;
                border[1 + s] = {cx + Ro * std::cos(a), cy + Ro * std::sin(a)};
            }
            border[2 + kArcSteps] = {cx + Ro * std::cos(a1),  cy + Ro * std::sin(a1)};
            border[3 + kArcSteps] = {cx + Rtb * std::cos(a1), cy + Rtb * std::sin(a1)};
            for (size_t j = 0; j < border.size(); ++j) {
                size_t k = (j + 1) % border.size();
                SDL_RenderLine(r, border[j].x, border[j].y, border[k].x, border[k].y);
            }
        }

        // Symbol (always shown if has section).
        if (hasSection) {
            const float symR = Rtb + (Ro - Rtb) * 0.30f; // near tooth base
            const float symSz = (Ro - Rtb) * 0.85f;
            sec->drawSymbol(r, cx + symR * std::cos(mid), cy + symR * std::sin(mid), symSz);
        }

        // Hover text label (shown only when hovering, using shared tooltip style).
        if (hovered && hasSection) {
            const float tipX = cx + (Ro + 4.f) * std::cos(mid);
            const float tipY = cy + (Ro + 4.f) * std::sin(mid);
            renderTooltip(r, sec->title(), tipX, tipY, worldRect());
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
            verts[static_cast<size_t>(i) + 1].position = {cx + Ri * std::cos(a), cy + Ri * std::sin(a)};
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
        SDL_RenderLine(r, cx + Ri * std::cos(a0), cy + Ri * std::sin(a0),
                       cx + Ri * std::cos(a1), cy + Ri * std::sin(a1));
    }

    // --- Center content ---
    {
        PrefSection* sec = sections_[activeSection_];
        if (sec && sec->hasContent()) {
            const float Ri2 = innerR();
            SDL_FRect innerBounds{cx - Ri2, cy - Ri2, Ri2 * 2.f, Ri2 * 2.f};
            sec->renderContent(r, innerBounds, scale);
        }
    }

    // --- Close button (small X in top-right of inner circle) ---
    {
        const float bx = cx + Ri * 0.55f;
        const float by = cy - Ri * 0.55f;
        const float bs = 14.f * scale;
        const float pad = 3.f * scale;
        SDL_FRect cb{bx - bs * 0.5f, by - bs * 0.5f, bs, bs};
        SDL_SetRenderDrawColor(r, 80, 80, 88, 255);
        SDL_RenderFillRect(r, &cb);
        SDL_SetRenderDrawColor(r, 200, 200, 210, 255);
        SDL_RenderLine(r, cb.x + pad, cb.y + pad, cb.x + cb.w - pad, cb.y + cb.h - pad);
        SDL_RenderLine(r, cb.x + cb.w - pad, cb.y + pad, cb.x + pad, cb.y + cb.h - pad);
    }
}

// --- Input ---

bool PreferencesWindow::handleInput(SDL_Event& e) {
    if (!visible) return false;

    float mx, my;
    SDL_GetMouseState(&mx, &my);

    // Only intercept mousedown for cog-specific controls; base handles everything else.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && hitTest(mx, my)) {
        // Close button?
        const float scalex = std::min(w, h) / 420.f;
        const float cx = centerX();
        const float cy = centerY();
        const float Ri = innerR();
        const float bx = cx + Ri * 0.55f;
        const float by = cy - Ri * 0.55f;
        const float bs = 14.f * scalex;
        if (mx >= bx - bs * 0.5f && mx < bx + bs * 0.5f &&
            my >= by - bs * 0.5f && my < by + bs * 0.5f) {
            close();
            return true;
        }

        // Tooth click?
        int tooth = hitTooth(mx, my);
        if (tooth >= 0 && sections_[tooth]) {
            activeSection_ = tooth;
            return true;
        }

        // Start drag from anywhere on the cog.
        dragging_ = true;
        dragOffX_ = mx - x;
        dragOffY_ = my - y;
        return true;
    }

    return EmbeddedWindow::handleInput(e);
}
