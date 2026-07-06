#pragma once

#include "WindowManager.h"
#include "PrefSection.h"
#include <array>

/// PreferencesWindow rendered in its own top-level SDL3 window.
class PreferencesExpandedWindow : public ExpandedWindow {
public:
    PreferencesExpandedWindow();
    ~PreferencesExpandedWindow() override;

    void render() override;
    bool handleInput(SDL_Event& e) override;
    bool handleKeyboard(SDL_Event& e) override;

    static constexpr int kTeeth = 8;

private:
    int activeSection_ = 0;

    AudioSection audio_;
    GUISection gui_;
    ControlsSection controls_;
    ColorsSection colors_;
    GeneralSection general_;
    std::array<PrefSection*, kTeeth> sections_{};

    void initSections();

    // Gear geometry helpers (relative to window center).
    float outerR(float w, float h) const { return std::min(w, h) * 0.45f; }
    float innerR(float w, float h) const { return outerR(w, h) * 0.74f; }
    float toothBaseR(float w, float h) const { return outerR(w, h) * 0.82f; }

    int hitTooth(float mx, float my, float w, float h) const;
};
