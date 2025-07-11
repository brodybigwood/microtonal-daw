#include "RegionManager.h"
#include "Project.h"
#include <SDL3/SDL_events.h>
#include <string>
#include "styles.h"
#include "Region.h"

RegionManager::RegionManager() {
    this->project = Project::instance();
    this->regions = &(project->regions);


}

RegionManager::~RegionManager() {

}

bool RegionManager::handleInput(SDL_Event& e) {
    bool running = true;
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if(hoverNew) {
                    project->createRegion();
                    currentRegion = (*regions).back();
                    break;
                }
                if(hoveredRegion != nullptr) {
                    currentRegion = hoveredRegion;
                    break;
                }
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            scrollY -= e.wheel.y * 10;
            if(scrollY < 0) {
                scrollY = 0;
            }
    }
    return running;
}

void RegionManager::render() {


    float height = 50;
    float topMargin = 5;
    float sideMargin = 5;
    float bottomMargin = 25;
    float allRegionsHeight = (*regions).size() * (height + topMargin) + topMargin + bottomMargin;



    if(allRegionsHeight < dstRect->h) {
        scrollY = 0;
    } else if (scrollY > allRegionsHeight - dstRect->h) {
        scrollY = allRegionsHeight - dstRect->h;
    }


    //background
    SDL_Rect clipRect = {
        static_cast<int>(dstRect->x),
        static_cast<int>(dstRect->y),
        static_cast<int>(dstRect->w),
        static_cast<int>(dstRect->h)
    };
    SDL_SetRenderClipRect(renderer, &clipRect);
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(renderer, dstRect);



    float i = topMargin + dstRect->y - scrollY;


    hoveredRegion = nullptr;

    //regions
    for(auto region : *regions) {
        SDL_FRect rect{dstRect->x + sideMargin, i, dstRect->w - 2*sideMargin, height};

        if (*mouseX >= rect.x && *mouseX < (rect.x + rect.w) &&
            *mouseY >= rect.y && *mouseY < (rect.y + rect.h)) {
            hoveredRegion = region;
            if(region == currentRegion) {
                SDL_SetRenderDrawColor(renderer, 120, 40, 40, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 40, 40, 120, 255);
            }
        } else {
            if(region == currentRegion) {
                SDL_SetRenderDrawColor(renderer, 60, 20, 20, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 20, 20, 60, 255);
            }
        }

        SDL_RenderFillRect(renderer, &rect);

        SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
        SDL_RenderRect(renderer, &rect);



        std::string regionIdText = std::to_string(region->id);
        SDL_Color textColor = {255, 255, 255, 255};

        SDL_Surface* textSurface = TTF_RenderText_Blended(fonts.mainFont, regionIdText.c_str(), regionIdText.size(), textColor);
        if (textSurface) {
            SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            if (textTexture) {
                SDL_FRect textRect;
                textRect.w = static_cast<float>(textSurface->w);
                textRect.h = static_cast<float>(textSurface->h);
                textRect.x = rect.x + (rect.w - textRect.w) / 2.0f;
                textRect.y = rect.y + (rect.h - textRect.h) / 2.0f;

                SDL_RenderTexture(renderer, textTexture, nullptr, &textRect);
                SDL_DestroyTexture(textTexture);
            }
            SDL_DestroySurface(textSurface);
        }





        i += height + topMargin;
    }

    //outline
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_RenderRect(renderer, dstRect);

    //new region btn
    SDL_FRect rect{dstRect->x, dstRect->y + dstRect->h - bottomMargin, dstRect->w, bottomMargin};
    hoverNew = false;
    if (*mouseX >= rect.x && *mouseX < (rect.x + rect.w) &&
        *mouseY >= rect.y && *mouseY < (rect.y + rect.h)) {
            SDL_SetRenderDrawColor(renderer, 40, 120, 40, 255);
            hoveredRegion = nullptr;
            hoverNew = true;
    } else {
            SDL_SetRenderDrawColor(renderer, 20, 60, 20, 255);
            hoverNew = false;
    }
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderRect(renderer, &rect);

    SDL_SetRenderClipRect(renderer, NULL);
}
