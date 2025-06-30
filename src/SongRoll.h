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
    SongRoll(int x, int y, int width, int height, SDL_Renderer* renderer, Project* project, WindowHandler* windowHandler);
        ~SongRoll();
        
        WindowHandler* windowHandler;
        SDL_Renderer* renderer;

        int cellHeight = 50;
        int cellWidth = 20;


        void render();

        SDL_Texture* texture;
        SDL_Texture* gridTexture;
        SDL_Texture* regionTexture;
        SDL_Texture* playHeadTexture;

        SDL_FRect dstRect;

        SDL_FRect regionRect;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
        void handleCustomInput(SDL_Event& e) override;

        void RenderGridTexture();

        void renderRegions();
        void renderRegion(int);

        void getHoveredRegion();

        int hoveredRegion = -1;

        void moveMouse();

        void clickMouse(SDL_Event& e);

        void createElement() override;

        int getHoveredTrack();
};

#endif
