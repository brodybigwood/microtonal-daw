#pragma once

#include "Window.h"

class Project;

/** Utility window that draws `UndoManager::render` for the undo graph. */
class UndoTreeWindow : public Window {
public:
    explicit UndoTreeWindow(Project* project);
    ~UndoTreeWindow();

    void renderFrame();
    void handleWindowInput(SDL_Event& e) override;

private:
    Project* project = nullptr;
    SDL_FRect rootCell{8.0f, 8.0f, 96.0f, 20.0f};
    SDL_Renderer* lastUndoRenderer = nullptr;
};
