#pragma once

#include "EmbeddedWindow.h"

class PreferencesWindow : public EmbeddedWindow {
public:
    PreferencesWindow();

    void render(SDL_Renderer* r) override;
    bool handleInput(SDL_Event& e) override;

private:
    int activeSection_ = 0;

    void buildHitPolygon(std::vector<SDL_FPoint>& out) const override;
    bool hasRectResize() const override { return false; }

    float centerX() const { return x + w * 0.5f; }
    float centerY() const { return y + h * 0.5f; }
    float outerR() const { return std::min(w, h) * 0.5f; }
    float innerR() const { return outerR() * 0.62f; }
    float toothBaseR() const { return outerR() * 0.72f; }

    /** Returns -1 if not on a tooth, or the tooth index. */
    int hitTooth(float worldMx, float worldMy) const;
};
