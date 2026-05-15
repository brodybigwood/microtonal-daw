#include "UndoTreeWindow.h"
#include "Project.h"
#include "UndoManager.h"
#include <SDL3/SDL.h>
#include <algorithm>

UndoTreeWindow::UndoTreeWindow(Project* p) : project(p) {
    title = "Undo Tree";
    w = 480.f;
    h = 640.f;
}

void UndoTreeWindow::renderContent(SDL_Renderer* r) {
    if (!project || !project->um) return;
    if (lastUndoRenderer != r) {
        project->um->clearAllRenderTextures();
        lastUndoRenderer = r;
    }

    constexpr float margin = 8.f;
    const float contentX = x + kBorderW;
    const float contentY = y + kTitleBarH;
    const float contentW = w - kBorderW * 2.f;
    const float contentH = h - kTitleBarH - kBorderW;
    const float colW = 160.f * zoom_;
    const float rowPx = 20.f * zoom_;
    project->um->undoTreeRowH = rowPx;
    rootCell = {contentX + margin, contentY + margin, colW, rowPx};

    SDL_FRect bg{contentX, contentY, contentW, contentH};
    SDL_SetRenderDrawColor(r, 248, 248, 248, 255);
    SDL_RenderFillRect(r, &bg);

    project->um->baseRect = &rootCell;
    project->um->render(r);
    project->um->baseRect = nullptr;
}

bool UndoTreeWindow::handleInput(SDL_Event& e) {
    if (!visible) return false;

    float mx, my;
    SDL_GetMouseState(&mx, &my);

    if (hitTest(mx, my) && e.type == SDL_EVENT_MOUSE_WHEEL && project && project->um) {
        constexpr float margin = 8.f;
        const float contentX = x + kBorderW;
        const float contentY = y + kTitleBarH;
        const float colW = 160.f * zoom_;
        const float rowPx = 20.f * zoom_;
        SDL_FRect anchor{contentX + margin, contentY + margin, colW, rowPx};
        project->um->undoTreeHandleWheel(anchor, rowPx, e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y);
        return true;
    }

    return EmbeddedWindow::handleInput(e);
}

bool UndoTreeWindow::handleContentInput(SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && project && project->um) {
        if (e.button.button == SDL_BUTTON_LEFT)
            project->um->undoTreePendingLeft = true;
        else if (e.button.button == SDL_BUTTON_RIGHT)
            project->um->undoTreePendingRight = true;
        return true;
    }
    return false;
}
