#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <cmath>
#include "GridView.h"
#include "styles.h"
#include "Project.h"
#include <iostream>
#include "WindowHandler.h"
#include "Playhead.h"

#ifndef SONGROLL_H
#define SONGROLL_H

class WindowHandler;  // forward declaration


class SongRoll : public GridView{

    public:
    SongRoll(SDL_FRect* rect, SDL_Renderer* renderer, bool* detached);
        ~SongRoll();
        
        WindowHandler* windowHandler;

        bool customTick() override;

        SDL_Texture* texture;
        SDL_Texture* gridTexture;
        SDL_Texture* regionTexture;
        SDL_Texture* playHeadTexture;

        SDL_FRect regionRect;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
        void handleCustomInput(SDL_Event& e) override;

        void RenderGridTexture();

        void renderRegions();
        void renderRegion(int);

        void getHoveredRegion();

        void clickMouse(SDL_Event& e) override;

        void createElement() override;
        void deleteElement() override;

        int getHoveredTrack();

        void UpdateGrid() override;
};

#endif
