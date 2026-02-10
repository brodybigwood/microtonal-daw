#include "ContextMenu.h"
#include "SDL_Events.h"
#include <iostream>
#include "styles.h"

ContextMenu* ContextMenu::get() {
    static ContextMenu c;
    return &c;
}

void ContextMenu::tick(SDL_Event& e) {
    if (!isEventForWindow(e, window_id)) return;

    if (!dynamicTick(e)) {
        active = false;
        return;
    }
    
    SDL_RenderPresent(renderer);
}

std::function<bool(SDL_Event& e)> getTextInputTicker(std::function<void(std::string text)> enter)
{

return [enter](SDL_Event& e) {
    auto ctxMenu = ContextMenu::get();
    auto& renderer = ctxMenu->renderer;
    auto& x = ctxMenu->locX;
    auto& y = ctxMenu->locY;

    SDL_FRect rect{x,y,100,100};

    bool done = false;

    static std::string text = "";
    static SDL_Surface* surf = nullptr;
    static SDL_Texture* tex = nullptr;
    static SDL_Color textColor{0,0,0,255};

    std::string new_text = text;

   if(e.type == SDL_EVENT_TEXT_INPUT) {
        new_text += e.text.text;
    }
        std::cout<<new_text<<std::endl;


    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.key == SDLK_BACKSPACE && !text.empty()) {
            new_text.pop_back();
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
        std::cout<<"RETURN"<<std::endl;

        enter(text);

        text = "";
        done = true;
        }

    if (done) {
        if (surf) SDL_DestroySurface(surf);
        if (tex) SDL_DestroyTexture(tex);

        surf = nullptr;
        tex = nullptr;
    } else {
        SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 0,0,0,255);
        SDL_RenderRect(renderer, &rect);

        if (surf && tex) {
            SDL_FRect dst{ x + 10, y + 10, surf->w, surf->h };
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
    }

    return !done;
};
}

std::function<bool(SDL_Event& e)> getTreeMenuTicker(std::shared_ptr<TreeEntry> t)
{
    auto listTick = std::make_shared<
        std::function<bool(SDL_Event&, std::shared_ptr<TreeEntry>, int, int, SDL_Renderer*)>
    >();
    
    *listTick = [listTick] (SDL_Event& e, std::shared_ptr<TreeEntry> t, int x, int y, SDL_Renderer* renderer) {

        float padding = 5.0f;
        SDL_FRect rect{x, y, padding * 2, 10.0f};

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

        for (auto c : t->children) {

            if (MouseOn(&rect)) {
                SDL_SetRenderDrawColor(renderer, 250, 250, 250, 255);

                switch (e.type) {
                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        if (e.button.button == SDL_BUTTON_LEFT) {
                            if (c->isParent()) {
                                c->isOpen = true;
                                for (auto k : t->children) if (c != k) k->isOpen = false;
                            } else {
                                c->click();
                                return false;
                            }
                        }
                        break;
                    default:
                        break;
                }
            } else {
                SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
            }
            SDL_RenderFillRect(renderer, &rect);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderRect(renderer, &rect);

            SDL_FRect textRect{rect.x + padding, rect.y, c->textWidth, c->textHeight};
            SDL_RenderTexture(renderer, c->labelTexture, nullptr, &textRect);

            if (c->isOpen) if (!(*listTick)(e, c, rect.x + rect.w, rect.y, renderer)) return false;

            rect.y += rect.h;
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
