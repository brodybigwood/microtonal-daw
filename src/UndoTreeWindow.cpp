#include "UndoTreeWindow.h"
#include "Project.h"
#include "UndoManager.h"
#include "NodeProcessor.h"
#include "WindowHandler.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

UndoTreeWindow::UndoTreeWindow(Project* p) : project(p) {
    window = SDL_CreateWindow("Undo", 480, 640, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);
    SDL_SetWindowResizable(window, true);
    SDL_SetWindowPosition(window, 420, 80);
    if (project && project->processor) {
        if (SDL_Window* parent = project->processor->getHostWindow()) {
            const SDL_WindowFlags hostFlags = SDL_GetWindowFlags(parent);
            if ((hostFlags & SDL_WINDOW_HIDDEN) == 0)
                SDL_SetWindowParent(window, parent);
        }
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    WindowHandler::instance()->addWindow(this);
    SDL_ShowWindow(window);
}

UndoTreeWindow::~UndoTreeWindow() {
    WindowHandler::instance()->removeWindow(this);
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
}

void UndoTreeWindow::renderFrame() {
    if (!project || !project->um || !renderer || !window)
        return;
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN)
        return;
    if (lastUndoRenderer != renderer) {
        project->um->clearAllRenderTextures();
        lastUndoRenderer = renderer;
    }

    SDL_SetRenderDrawColor(renderer, 248, 248, 248, 255);
    SDL_RenderClear(renderer);

    /* Narrow tree column like the original (96×20): row textures must not stretch to full window width. */
    const float margin = 8.f;
    int ww = 480;
    int wh = 640;
    SDL_GetWindowSize(window, &ww, &wh);
    const float usableW = static_cast<float>(ww) - 2.f * margin;
    const float colW = std::clamp(usableW, 96.f, 320.f);
    constexpr float kRowPx = 20.f;
    project->um->undoTreeRowH = kRowPx;
    rootCell = {margin, margin, colW, kRowPx};

    project->um->hitTestWindow = window;
    project->um->baseRect = &rootCell;
    project->um->render(renderer);
    project->um->hitTestWindow = nullptr;
    project->um->baseRect = nullptr;

    SDL_RenderPresent(renderer);
}

void UndoTreeWindow::handleWindowInput(SDL_Event& e) {
    if (!project || !project->um)
        return;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        if (e.button.button == SDL_BUTTON_LEFT)
            project->um->undoTreePendingLeft = true;
        else if (e.button.button == SDL_BUTTON_RIGHT)
            project->um->undoTreePendingRight = true;
    } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        int ww = 480;
        int whIgnored = 640;
        SDL_GetWindowSize(window, &ww, &whIgnored);
        const float margin = 8.f;
        const float usableW = static_cast<float>(ww) - 2.f * margin;
        const float colW = std::clamp(usableW, 96.f, 320.f);
        constexpr float kRowPx = 20.f;
        SDL_FRect anchor{margin, margin, colW, kRowPx};
        project->um->undoTreeHandleWheel(anchor, kRowPx, e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y);
    }
}
