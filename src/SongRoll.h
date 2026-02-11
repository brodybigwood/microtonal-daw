#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <cmath>
#include "GridElement.h"
#include "GridView.h"
#include "styles.h"
#include "Project.h"
#include <iostream>
#include "WindowHandler.h"
#include "Playhead.h"

#include "TrackList.h"

#ifndef SONGROLL_H
#define SONGROLL_H

class WindowHandler;  // forward declaration
class RegionManager;

class SongRoll : public GridView{

    public:
    SongRoll(SDL_FRect* rect, bool* detached);
        ~SongRoll() override;

        TrackList* tracks;
        SDL_FRect leftRect;
        
        WindowHandler* windowHandler;

        void renderMargins();
        void createElement() override;

        float getY(float) override;

        bool customTick() override;

        SDL_Texture* texture;
        SDL_Texture* regionTexture;
        SDL_Texture* playHeadTexture;

        SDL_FRect regionRect;
        SDL_FRect rightRect;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
        void handleCustomInput(SDL_Event& e) override;

        void renderElements();
        void renderElement(GridElement*);

        void getHoveredRegion();

        void clickMouse(SDL_Event& e) override;

        void deleteElement() override;

        void UpdateGrid() override;

        float getHoveredLine();

        void beginDrop(SDL_DropEvent&) override;
        void renderDrop(SDL_Renderer*) override;
        void dropFile(SDL_DropEvent&) override;        
};

#endif
