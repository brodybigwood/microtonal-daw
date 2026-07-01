#include "UndoTreeExpandedWindow.h"
#include "Project.h"
#include "UndoManager.h"
#include <SDL3/SDL.h>

UndoTreeExpandedWindow::UndoTreeExpandedWindow(Project* p) : project(p) {
    title = "Undo Tree";
}

UndoTreeExpandedWindow::~UndoTreeExpandedWindow() {}

void UndoTreeExpandedWindow::render() {
    if (!renderer || !project || !project->um) return;

    if (lastUndoRenderer != renderer) {
        project->um->clearAllRenderTextures();
        lastUndoRenderer = renderer;
    }

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);
    float w = static_cast<float>(winW);
    float h = static_cast<float>(winH);

    constexpr float margin = 8.f;
    const float colW = 160.f * zoom_;
    const float rowPx = 20.f * zoom_;
    project->um->undoTreeRowH = rowPx;
    rootCell = {margin, margin, colW, rowPx};

    SDL_SetRenderDrawColor(renderer, 248, 248, 248, 255);
    SDL_RenderClear(renderer);

    project->um->baseRect = &rootCell;
    project->um->hitTestWindow = window;
    project->um->render(renderer);
    project->um->hitTestWindow = nullptr;
    project->um->baseRect = nullptr;
}

bool UndoTreeExpandedWindow::handleInput(SDL_Event& e) {
    if (!project || !project->um) return false;

    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        const float colW = 160.f * zoom_;
        const float rowPx = 20.f * zoom_;
        constexpr float margin = 8.f;
        SDL_FRect anchor{margin, margin, colW, rowPx};
        project->um->undoTreeHandleWheel(anchor, rowPx, e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y);
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (e.button.button == SDL_BUTTON_LEFT)
            project->um->undoTreePendingLeft = true;
        else if (e.button.button == SDL_BUTTON_RIGHT)
            project->um->undoTreePendingRight = true;
        return true;
    }
    return false;
}

bool UndoTreeExpandedWindow::handleKeyboard(SDL_Event& e) {
    return false;
}
