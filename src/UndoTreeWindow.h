#pragma once

#include "Window.h"

class Project;

/** Utility window that draws `UndoManager::render` / `renderAction` so you can see `current` in the tree. */
class UndoTreeWindow : public Window {
public:
    explicit UndoTreeWindow(Project* project);
    ~UndoTreeWindow();

    void renderFrame();
    void handleWindowInput(SDL_Event& e) override;

private:
    Project* project = nullptr;
    SDL_FRect rootCell{8.0f, 8.0f, 96.0f, 26.0f};
    SDL_Renderer* lastUndoRenderer = nullptr;
};
