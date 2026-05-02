#include "UndoTreeWindow.h"
#include "Project.h"
#include "UndoManager.h"
#include "NodeProcessor.h"
#include "WindowHandler.h"
#include <SDL3/SDL.h>

UndoTreeWindow::UndoTreeWindow(Project* p) : project(p) {
    window = SDL_CreateWindow("Undo", 480, 640, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);
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
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
        project->um->clicked = true;
}
