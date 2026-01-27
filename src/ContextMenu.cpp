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
        SDL_SetRenderDrawColor(renderer, 0,0,0,0);
        SDL_RenderRect(renderer, &rect);

        if (surf && tex) {
            SDL_FRect dst{ x + 10, y + 10, surf->w, surf->h };
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
    }

    return !done;
};
}

