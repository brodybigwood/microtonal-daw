#pragma once

#include "WindowManager.h"

class Project;

/// UndoTreeWindow rendered in its own top-level SDL3 window.
class UndoTreeExpandedWindow : public ExpandedWindow {
public:
    explicit UndoTreeExpandedWindow(Project* project);
    ~UndoTreeExpandedWindow() override;

    void render() override;
    bool handleInput(SDL_Event& e) override;
    bool handleKeyboard(SDL_Event& e) override;

private:
    Project* project = nullptr;
    SDL_FRect rootCell{8.0f, 8.0f, 96.0f, 20.0f};
    SDL_Renderer* lastUndoRenderer = nullptr;
    float zoom_ = 1.0f;
};
