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

#ifndef SONGROLL_H
#define SONGROLL_H

class WindowHandler;  // forward declaration
class RegionManager;

class SongRoll : public GridView{

    public:
    SongRoll(SDL_FRect* rect, bool* detached);
        ~SongRoll();
        
        WindowHandler* windowHandler;

        InstrumentList* insts;

        void renderMargins();
        void createElement() override;

        std::vector<Instrument*>* instruments;

        float getY(float) override;

        bool customTick() override;

        SDL_Texture* texture;
        SDL_Texture* regionTexture;
        SDL_Texture* playHeadTexture;

        SDL_FRect regionRect;
        SDL_FRect rightRect;

        RegionManager* regionManager;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
        void handleCustomInput(SDL_Event& e) override;

        void renderRegions();
        void renderRegion(GridElement*);

        void getHoveredRegion();

        void clickMouse(SDL_Event& e) override;

        void deleteElement() override;

        void UpdateGrid() override;

        float getHoveredLine();
};

#endif
