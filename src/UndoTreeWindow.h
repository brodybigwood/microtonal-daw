#pragma once

#include "EmbeddedWindow.h"

class Project;

class UndoTreeWindow : public EmbeddedWindow {
public:
    explicit UndoTreeWindow(Project* project);

    bool trackMoveResizeInUndo() const override { return false; }

protected:
    void renderContent(SDL_Renderer* r) override;
    bool handleContentInput(SDL_Event& e) override;

private:
    Project* project = nullptr;
    SDL_FRect rootCell{8.0f, 8.0f, 96.0f, 20.0f};
    SDL_Renderer* lastUndoRenderer = nullptr;
};
