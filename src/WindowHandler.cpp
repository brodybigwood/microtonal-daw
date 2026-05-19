#include "WindowHandler.h"
#include "SDL_Events.h"
#include "Settings.h"
#include "styles.h"
#include "NodeEditor.h"
#include "Node.h"
#include <algorithm>
#include <cstdio>
#include <vector>

WindowHandler::WindowHandler() {

    SDL_SetHint(SDL_HINT_APP_NAME, "EDITOR");
    SDL_SetHint(SDL_HINT_APP_ID, "daw.editor");

    lastTime = SDL_GetTicks();

    ctxMenu = ContextMenu::get();
}

WindowHandler::~WindowHandler() {
}

WindowHandler* WindowHandler::instance() {
    static WindowHandler w;
    return &w;
}

bool WindowHandler::tick() {

    if (!running) return false;

    double timeSinceLastFrame = double(SDL_GetTicks())-double(lastTime);
    if(timeSinceLastFrame >= frameTime) {
        {
            float instant = (timeSinceLastFrame > 0.0) ? static_cast<float>(1000.0 / timeSinceLastFrame) : 60.f;
            fpsCounter_ = fpsCounter_ * 0.9f + instant * 0.1f;
        }
        lastTime = double(SDL_GetTicks())-frameTime;

        clearPendingTooltip();

        if (ctxMenu->active) { project->render(); }

        bool eventHandled = false;

        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            eventHandled = true;

            if (e.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                bool handledClose = false;
                SDL_Window* closingWindow = SDL_GetWindowFromID(e.window.windowID);
                if (closingWindow) {
                    for (auto* w : windows) {
                        if (!w || w->window != closingWindow) continue;
                        if (dynamic_cast<Node*>(w)) {
                        } else if (project && project->window && closingWindow != project->window) {
                            // Non-host utility windows should close themselves, not quit the app.
                            SDL_HideWindow(closingWindow);
                            handledClose = true;
                        }
                        break;
                    }
                }
                if (handledClose) continue;
                running = false;
                break;
            }


            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                SDL_Window* clickedWindow = SDL_GetWindowFromID(e.button.windowID);
                if (clickedWindow) {
                    SDL_RaiseWindow(clickedWindow);
                }
            }

            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.scancode == SDL_SCANCODE_SPACE) {
                    project->togglePlaying();
                    continue;
                }

                const SDL_Keymod mods = SDL_GetModState();
                const bool ctrlDown = (mods & SDL_KMOD_CTRL) != 0;
                const bool shiftDown = (mods & SDL_KMOD_SHIFT) != 0;
                // Use scancode (physical Z) — e.key.key == SDLK_Z often misses with modifiers / some layouts / SDL3.
                const bool zKey = (e.key.scancode == SDL_SCANCODE_Z) || (e.key.key == SDLK_Z);
                if (ctrlDown && zKey) {
                    // SDL key-repeat would fire undo/redo many times per key hold; undo tree navigates once per click.
                    if (e.key.repeat)
                        continue;
                    if (shiftDown) {
                        project->redo();
                    } else {
                        if (project->um->current == project->um->head) continue;
                        project->undo();
                    }
                    continue;
                }
                if (ctrlDown) {
                    if (e.key.key == SDLK_S) {
                        SDL_Renderer* eventRenderer = nullptr;
                        for (auto* w : windows) {
                            if (w && SDL_GetWindowFromID(e.key.windowID) == w->window) {
                                eventRenderer = w->renderer;
                                break;
                            }
                        }
                        project->save(e.key.windowID, eventRenderer);
                        continue;
                    }
                }
            }

            toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
            toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
            toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);

            if (ctxMenu->active) {
                ctxMenu->tick(e);
            } else {
                SDL_Window* eventWin = SDL_GetWindowFromID(getEventWindowID(e));
                for (auto w : windows)
                    if (w && eventWin == w->window) {
                        w->handleWindowInput(e);
                        break;
                    }
                if (project && eventWin == project->window)
                    project->handleWindowInput(e);
            }
        }

        if (!running) return false;

        if (!eventHandled && ctxMenu->active) { // home was already rendered, but ctxMenu hasn't rendered yet. so render on top by triggering with fake event
            e.type = SDL_EVENT_USER;
            e.window.windowID = ctxMenu->window_id;
            ctxMenu->tick(e);
        } else if (!ctxMenu->active) {
            project->render();
        }
        drawPendingTooltip();
        if (Settings::instance().showFps() && fonts.mainFont) {
            SDL_Renderer* fpsR = project ? project->renderer : nullptr;
            SDL_Window* fpsW = project ? project->window : nullptr;
            if (fpsR) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f", fpsCounter_);
                SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, buf, 0, SDL_Color{180, 220, 180, 220});
                if (sf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(fpsR, sf);
                    if (tex) {
                        float fw = static_cast<float>(sf->w), fh = static_cast<float>(sf->h);
                        int winW = 800, winH = 0;
                        if (fpsW) SDL_GetWindowSize(fpsW, &winW, &winH);
                        SDL_FRect dst{static_cast<float>(winW) - fw - 8.f, 8.f, fw, fh};
                        SDL_RenderTexture(fpsR, tex, nullptr, &dst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(sf);
                }
            }
        }
        project->renderPresent();
    }
    // Compact nulled-out window pointers (deferred from removeWindow during iteration).
    windows.erase(std::remove(windows.begin(), windows.end(), nullptr), windows.end());
    return running;

}

void WindowHandler::toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar) {

    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        if (e.key.scancode == keycode) {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                keyVar = true;
            } else if (e.type == SDL_EVENT_KEY_UP) {
                keyVar = false;
            }
        }
    }
}

void WindowHandler::addWindow(Window* w) {
    windows.push_back(w);
}

void WindowHandler::removeWindow(Window* w) {
    auto it = std::find(windows.begin(), windows.end(), w);
    if (it != windows.end()) {
        *it = nullptr;  // null out to keep iterators valid; compacted at end of tick()
    }
}

