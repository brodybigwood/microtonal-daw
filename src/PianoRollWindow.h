#pragma once

#include "WindowManager.h"
#include "PianoRoll.h"

/// Wraps a PianoRoll in its own top-level SDL3 window.
class PianoRollWindow : public ExpandedWindow {
public:
    PianoRollWindow(Region* region, Window* parent);
    ~PianoRollWindow() override;

    void render() override;
    bool handleInput(SDL_Event& e) override;
    bool handleKeyboard(SDL_Event& e) override;
    void onCreated() override;

    PianoRoll* getPianoRoll() { return pianoRoll_; }

private:
    PianoRoll* pianoRoll_ = nullptr;  // owned, but also registered as EmbeddedWindow for now
    bool needsInit_ = true;
    int lastW_ = 0, lastH_ = 0;
};
