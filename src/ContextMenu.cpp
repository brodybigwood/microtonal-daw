#include "ContextMenu.h"
#include "SDL_Events.h"
#include <algorithm>
#include <iostream>
#include "styles.h"
#include "WindowHandler.h"

ContextMenu* ContextMenu::get() {
    static ContextMenu c;
    return &c;
}

void ContextMenu::activate() {
    // Use the currently focused window, falling back to the project window.
    SDL_Window* focused = SDL_GetMouseFocus();
    if (focused) {
        window_id = SDL_GetWindowID(focused);
        renderer = SDL_GetRenderer(focused);
    }
    if (!renderer) {
        auto* wh = WindowHandler::instance();
        if (wh && wh->project && wh->project->window) {
            window_id = SDL_GetWindowID(wh->project->window);
            renderer = wh->project->renderer;
        }
    }
    SDL_GetMouseState(&locX, &locY);
    active = true;
    // Enable text input on this window for text fields.
    if (focused) SDL_StartTextInput(focused);
}

void ContextMenu::activate(SDL_Renderer* r, uint32_t wid) {
    renderer = r;
    window_id = wid;
    SDL_GetMouseState(&locX, &locY);
    active = true;
    // Enable text input on this window for text fields.
    SDL_Window* w = SDL_GetWindowFromID(wid);
    if (w) SDL_StartTextInput(w);
}

void ContextMenu::tick(SDL_Event& e) {
    // Dismiss if the event is for a different window.
    if (!isEventForWindow(e, window_id)) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            active = false;
        }
        return;
    }

    if (skipNextEvent) {
        skipNextEvent = false;
        return;
    }

    if (!dynamicTick(e)) {
        active = false;
        return;
    }
}

std::function<bool(SDL_Event& e)> getTextInputTicker(std::function<void(std::string text)> enter,
                                                     std::function<void()> onDismiss,
                                                     const std::string& initialText)
{

return [enter, onDismiss, initialText](SDL_Event& e) {
    auto ctxMenu = ContextMenu::get();
    auto& renderer = ctxMenu->renderer;
    auto& x = ctxMenu->locX;
    auto& y = ctxMenu->locY;

    SDL_FRect rect{x, y, 180.f, 30.f};

    bool done = false;

    static std::string text = "";
    static bool inited = false;
    if (!inited) {
        text = initialText;
        inited = true;
    }
    static SDL_Surface* surf = nullptr;
    static SDL_Texture* tex = nullptr;
    static SDL_Color textColor{0,0,0,255};

    std::string new_text = text;

    if (!MouseOn(&rect) && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) done = true;

   if(e.type == SDL_EVENT_TEXT_INPUT) {
        new_text += e.text.text;
    }

    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.key == SDLK_BACKSPACE && !text.empty()) {
            new_text.pop_back();
        }
        if (e.key.key == SDLK_ESCAPE) {
            done = true;
        }
    }

    if (!surf || text != new_text) surf = TTF_RenderText_Blended(fonts.mainFont, new_text.c_str(), 0, textColor);
    if (!tex || text != new_text) tex = SDL_CreateTextureFromSurface(renderer, surf);

    text = new_text;

    if ( (
        e.type == SDL_EVENT_TEXT_INPUT && e.text.text[0] == '\n'
        ) || (
        e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN
        )
        ) {

        enter(text);

        text = "";
        done = true;
        }

    if (done) {
        if (surf) SDL_DestroySurface(surf);
        if (tex) SDL_DestroyTexture(tex);

        surf = nullptr;
        tex = nullptr;
        inited = false;
        if (onDismiss) onDismiss();
    } else {
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 0,0,0,255);
        SDL_RenderRect(renderer, &rect);

        if (surf && tex) {
            SDL_FRect dst{ x + 10, y + 10, (float)surf->w, (float)surf->h };
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
    }

    return !done;
};
}

/** Recursively check if the mouse is on any visible part of the tree rooted at (x, y). */
static bool mouseOnTree(std::shared_ptr<TreeEntry> t, float x, float y) {
    float padding = 5.0f;
    SDL_FRect rect{x, y, padding * 2, 10.0f};

    for (auto& c : t->children) {
        if (c->textWidth + padding * 2 > rect.w) rect.w = static_cast<float>(c->textWidth) + padding * 2;
        if (c->textHeight > rect.h) rect.h = static_cast<float>(c->textHeight);
    }

    for (auto& c : t->children) {
        if (MouseOn(&rect)) return true;

        if (c->isOpen) {
            if (c->customTick) {
                SDL_FRect customArea{
                    rect.x + rect.w,
                    rect.y,
                    c->customWidth > 0 ? c->customWidth : 0,
                    c->customHeight > 0 ? c->customHeight : rect.h
                };
                if (MouseOn(&customArea)) return true;
                if (!c->children.empty()) {
                    if (mouseOnTree(c, rect.x + rect.w + c->customWidth, rect.y)) return true;
                }
            } else if (!c->children.empty()) {
                if (mouseOnTree(c, rect.x + rect.w, rect.y)) return true;
            }
        }
        rect.y += rect.h;
    }
    return false;
}

std::function<bool(SDL_Event& e)> getTreeMenuTicker(std::shared_ptr<TreeEntry> t)
{
    auto listTick = std::make_shared<
        std::function<bool(SDL_Event&, std::shared_ptr<TreeEntry>, int, int, SDL_Renderer*)>
    >();
    
    *listTick = [listTick] (SDL_Event& e, std::shared_ptr<TreeEntry> t, int x, int y, SDL_Renderer* renderer) {

        float padding = 5.0f;
        SDL_FRect rect{(float)x, (float)y, padding * 2, 10.0f};

        for (auto c : t->children) {
            if (!c->labelTexture) {
                SDL_Color textColor{0,0,0,255};
                SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, c->label.c_str(), 0, textColor);
                c->labelTexture = SDL_CreateTextureFromSurface(renderer, surf);
                c->textWidth = surf->w;
                c->textHeight = surf->h;
                SDL_DestroySurface(surf);
            }

            if (c->textWidth > rect.w) rect.w = c->textWidth + padding * 2;
            if (c->textHeight > rect.h) rect.h = c->textHeight;
        }

        // Scrollable children list
        const bool scrollable = (t->maxListHeight > 0.f);
        float listTop = static_cast<float>(y);
        float listH = scrollable ? t->maxListHeight : 1e9f;
        float contentH = static_cast<float>(t->children.size()) * rect.h;
        float maxScroll = std::max(0.f, contentH - listH);
        if (t->scrollOffset > maxScroll) t->scrollOffset = maxScroll;
        if (t->scrollOffset < 0.f) t->scrollOffset = 0.f;

        if (scrollable && e.type == SDL_EVENT_MOUSE_WHEEL) {
            float mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            SDL_FRect listRect{(float)x, listTop, rect.w, listH};
            if (mx >= listRect.x && mx < listRect.x + listRect.w &&
                my >= listRect.y && my < listRect.y + listRect.h) {
                t->scrollOffset -= e.wheel.y * rect.h;
                t->scrollOffset = std::clamp(t->scrollOffset, 0.f, maxScroll);
                e.type = SDL_EVENT_FIRST; // consume wheel
            }
        }

        float baseY = rect.y;
        float clippedBottom = listTop + listH;
        bool mouseOn = false;
        for (auto c : t->children) {
            float drawY = baseY - t->scrollOffset;
            bool visible = (drawY + rect.h > listTop && drawY < clippedBottom);

            // Update rect.y for hit testing
            rect.y = drawY;

            bool clickedNow = false;
            if (visible && MouseOn(&rect)) {
                mouseOn = true;

                SDL_SetRenderDrawColor(renderer, 250, 250, 250, 255);

                switch (e.type) {
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        clickedNow = true;
                        if (e.button.button == SDL_BUTTON_LEFT) {
                            if (c->isParent()) {
                                c->isOpen = true;
                                c->scrollOffset = 0.f;
                                for (auto k : t->children) if (c != k) k->isOpen = false;
                            } else {
                                c->click();
                                auto* ctx = ContextMenu::get();
                                if (ctx->keepOpenOnNextTreeLeafClick) {
                                    ctx->keepOpenOnNextTreeLeafClick = false;
                                    return true;
                                }
                                return false;
                            }
                        }
                        break;
                    default:
                        break;
                }
            } else if (visible) {
                SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
            }

            if (visible) {
                SDL_RenderFillRect(renderer, &rect);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderRect(renderer, &rect);

                SDL_FRect textRect{rect.x + padding, rect.y, (float)c->textWidth, (float)c->textHeight};
                SDL_RenderTexture(renderer, c->labelTexture, nullptr, &textRect);
            }

            if (c->isOpen && !clickedNow) {
                if (c->customTick) {
                    const float customX = static_cast<float>(rect.x + rect.w);
                    if (!c->customTick(e, customX, static_cast<float>(rect.y), renderer, c)) {
                        c->isOpen = false;
                    } else if (!c->children.empty()) {
                        const float childX = customX + c->customWidth;
                        if (!(*listTick)(e, c, static_cast<int>(childX), rect.y, renderer)) return false;
                    }
                } else {
                    if (!(*listTick)(e, c, rect.x + rect.w, rect.y, renderer)) return false;
                }
            }

            baseY += rect.h;
        }

        // exit the menu if clicked somewhere outside the visible tree
        if (!mouseOn && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            if (!mouseOnTree(t, static_cast<float>(x), static_cast<float>(y))) return false;
        }
        return true;
    };
    return [t,listTick] (SDL_Event& e)
    {
        auto ctxMenu = ContextMenu::get();
        auto& renderer = ctxMenu->renderer;
        auto& x = ctxMenu->locX;
        auto& y = ctxMenu->locY;

        return (*listTick)(e, t, x, y, renderer);
    };
}
