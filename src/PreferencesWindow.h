#pragma once

#include "EmbeddedWindow.h"
#include "PrefSection.h"
#include <array>

class PreferencesWindow : public EmbeddedWindow {
public:
    PreferencesWindow();

    void render(SDL_Renderer* r) override;
    bool handleInput(SDL_Event& e) override;

    static constexpr int kTeeth = 8;

private:
    int activeSection_ = 0;

    AudioSection audio_;
    GUISection gui_;
    ControlsSection controls_;
    std::array<PrefSection*, kTeeth> sections_{};

    void initSections();

    void buildHitPolygon(std::vector<SDL_FPoint>& out) const override;
    bool hasRectResize() const override { return false; }

    float centerX() const { return x + w * 0.5f; }
    float centerY() const { return y + h * 0.5f; }
    float outerR() const { return std::min(w, h) * 0.5f; }
    float innerR() const { return outerR() * 0.74f; }
    float toothBaseR() const { return outerR() * 0.82f; }

    /** Returns -1 if not on a tooth, or the tooth index. */
    int hitTooth(float worldMx, float worldMy) const;
};
