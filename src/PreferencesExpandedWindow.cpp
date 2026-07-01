#include "PreferencesExpandedWindow.h"
#include "styles.h"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PreferencesExpandedWindow::PreferencesExpandedWindow() {
    title = "Preferences";
    initSections();
}

PreferencesExpandedWindow::~PreferencesExpandedWindow() {}

void PreferencesExpandedWindow::initSections() {
    sections_ = {};
    sections_[0] = &audio_;
    sections_[1] = &gui_;
    sections_[2] = &controls_;
    sections_[3] = &general_;
}

int PreferencesExpandedWindow::hitTooth(float mx, float my, float w, float h) const {
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float dx = mx - cx;
    const float dy = my - cy;
    const float Ro = outerR(w, h);
    const float Rtb = toothBaseR(w, h);

    const float sector = 2.f * static_cast<float>(M_PI) / static_cast<float>(kTeeth);
    const float toothHalf = sector * 0.35f;

    for (int i = 0; i < kTeeth; ++i) {
        float mid = static_cast<float>(i) * sector;
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

void PreferencesExpandedWindow::render() {
    if (!renderer) return;

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    float w = static_cast<float>(winW);
    float h = static_cast<float>(winH);

    SDL_SetRenderDrawColor(renderer, 30, 30, 34, 255);
    SDL_RenderClear(renderer);

    const float scale = std::min(w, h) / 420.f;
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float Ro = outerR(w, h);
    const float Ri = innerR(w, h);
    const float Rtb = toothBaseR(w, h);
    const int N = kTeeth;
    const float sector = 2.f * static_cast<float>(M_PI) / static_cast<float>(N);
    const float toothHalf = sector * 0.35f;
    const int kArcSteps = 4;

    // Build gear polygon.
    std::vector<SDL_FPoint> poly;
    for (int i = 0; i < N; ++i) {
        const float mid = static_cast<float>(i) * sector;
        const float a0 = mid - toothHalf;
        const float a1 = mid + toothHalf;
        const float aNext = (i == N - 1) ? (2.f * static_cast<float>(M_PI)) : (static_cast<float>(i + 1) * sector);
        const float aGapStart = a1;
        const float aGapEnd = aNext - toothHalf;

        poly.push_back({cx + Ri * std::cos(a0), cy + Ri * std::sin(a0)});
        poly.push_back({cx + Ro * std::cos(a0), cy + Ro * std::sin(a0)});
        for (int s = 1; s <= kArcSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(kArcSteps + 1);
            float a = a0 + (a1 - a0) * t;
            poly.push_back({cx + Ro * std::cos(a), cy + Ro * std::sin(a)});
        }
        poly.push_back({cx + Ro * std::cos(a1), cy + Ro * std::sin(a1)});
        poly.push_back({cx + Ri * std::cos(a1), cy + Ri * std::sin(a1)});
        for (int s = 1; s <= kArcSteps; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(kArcSteps + 1);
            float a = aGapStart + (aGapEnd - aGapStart) * t;
            poly.push_back({cx + Ri * std::cos(a), cy + Ri * std::sin(a)});
        }
    }

    // Fill body.
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
        SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()),
                           idx.data(), static_cast<int>(idx.size()));
    }

    // Body outline.
    SDL_SetRenderDrawColor(renderer, 90, 90, 96, 255);
    for (size_t i = 0; i < poly.size(); ++i) {
        size_t j = (i + 1) % poly.size();
        SDL_RenderLine(renderer, poly[i].x, poly[i].y, poly[j].x, poly[j].y);
    }

    // Tooth buttons.
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    const int hoveredTooth = hitTooth(mx, my, w, h);

    for (int i = 0; i < N; ++i) {
        const float mid = static_cast<float>(i) * sector;
        const float a0 = mid - toothHalf;
        const float a1 = mid + toothHalf;

        PrefSection* sec = sections_[i];
        const bool isActive = (i == activeSection_);
        const bool hasSection = (sec != nullptr);
        const bool hovered = (i == hoveredTooth);

        SDL_FColor col;
        if (isActive) col = {80.f/255.f, 130.f/255.f, 200.f/255.f, 1.f};
        else if (hovered && hasSection) col = {0.255f, 0.255f, 0.282f, 1.f};
        else if (hasSection) col = {0.216f, 0.216f, 0.235f, 1.f};
        else col = {0.165f, 0.165f, 0.180f, 1.f};

        {
            const int nv = 4 + kArcSteps;
            std::vector<SDL_Vertex> verts(static_cast<size_t>(nv));
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
            SDL_RenderGeometry(renderer, nullptr, verts.data(), nv,
                               indices.data(), static_cast<int>(indices.size()));
        }

        // Symbol.
        if (hasSection) {
            const float symR = Rtb + (Ro - Rtb) * 0.30f;
            const float symSz = (Ro - Rtb) * 0.85f;
            sec->drawSymbol(renderer, cx + symR * std::cos(mid), cy + symR * std::sin(mid), symSz);
        }
    }

    // Inner circle.
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
        SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()),
                           idx.data(), static_cast<int>(idx.size()));
    }

    // Inner circle border.
    SDL_SetRenderDrawColor(renderer, 80, 80, 88, 255);
    for (int i = 0; i < circleSegs; ++i) {
        float a0 = static_cast<float>(i) * 2.f * static_cast<float>(M_PI) / static_cast<float>(circleSegs);
        float a1 = static_cast<float>(i + 1) * 2.f * static_cast<float>(M_PI) / static_cast<float>(circleSegs);
        SDL_RenderLine(renderer, cx + Ri * std::cos(a0), cy + Ri * std::sin(a0),
                       cx + Ri * std::cos(a1), cy + Ri * std::sin(a1));
    }

    // Center content.
    {
        PrefSection* sec = sections_[activeSection_];
        if (sec && sec->hasContent()) {
            SDL_FRect innerBounds{cx - Ri, cy - Ri, Ri * 2.f, Ri * 2.f};
            sec->contentScale_ = scale;
            sec->renderContent(renderer, innerBounds, scale);
        }
    }

    // Close button.
    {
        const float bx = cx + Ri * 0.55f;
        const float by = cy - Ri * 0.55f;
        const float bs = 14.f * scale;
        const float pad = 3.f * scale;
        SDL_FRect cb{bx - bs * 0.5f, by - bs * 0.5f, bs, bs};
        SDL_SetRenderDrawColor(renderer, 80, 80, 88, 255);
        SDL_RenderFillRect(renderer, &cb);
        SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
        SDL_RenderLine(renderer, cb.x + pad, cb.y + pad, cb.x + cb.w - pad, cb.y + cb.h - pad);
        SDL_RenderLine(renderer, cb.x + cb.w - pad, cb.y + pad, cb.x + pad, cb.y + cb.h - pad);
    }
}

bool PreferencesExpandedWindow::handleInput(SDL_Event& e) {
    if (!renderer) return false;

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    float w = static_cast<float>(winW);
    float h = static_cast<float>(winH);

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        float mx = e.button.x;
        float my = e.button.y;

        // Close button check.
        const float scale = std::min(w, h) / 420.f;
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        const float Ri = innerR(w, h);
        const float bx = cx + Ri * 0.55f;
        const float by = cy - Ri * 0.55f;
        const float bs = 14.f * scale;
        if (mx >= bx - bs * 0.5f && mx < bx + bs * 0.5f &&
            my >= by - bs * 0.5f && my < by + bs * 0.5f) {
            hide();
            return true;
        }

        // Tooth click.
        int tooth = hitTooth(mx, my, w, h);
        if (tooth >= 0 && sections_[tooth]) {
            activeSection_ = tooth;
            return true;
        }

        // Inner-circle content click.
        float d2 = (mx - cx) * (mx - cx) + (my - cy) * (my - cy);
        if (d2 <= Ri * Ri) {
            PrefSection* sec = sections_[activeSection_];
            if (sec) {
                SDL_FRect innerBounds{cx - Ri, cy - Ri, Ri * 2.f, Ri * 2.f};
                if (sec->handleContentInput(e, mx, my, innerBounds))
                    return true;
            }
        }
    }

    return false;
}

bool PreferencesExpandedWindow::handleKeyboard(SDL_Event& e) {
    return false;
}
