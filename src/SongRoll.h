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

#include "TrackManager.h"

#ifndef SONGROLL_H
#define SONGROLL_H

class WindowHandler;  // forward declaration
class ElementManager;
class ArrangerNode;

class SongRoll : public GridView{

    public:
    SongRoll(SDL_FRect* rect, bool* detached, Window*, Project*, ArrangerNode*);
        ~SongRoll() override;

        ArrangerNode* parentNode;
        TrackManager* tracks;
        ElementManager* em;
        SDL_FRect leftRect;
        
        WindowHandler* windowHandler;

        void renderMargins();
        void createElement() override;

        float getY(float) override;

        bool customTick() override;

        SDL_Texture* texture;
        SDL_Texture* regionTexture;
        SDL_Texture* playHeadTexture;

        void clearTextures() override;
        void generateTextures() override;

        SDL_FRect regionRect;
        SDL_FRect rightRect;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
        void handleCustomInput(SDL_Event& e) override;

        Uint32 doubleClickTime = 500; // 500ms
        Uint32 lastLmbTime;
        void doubleClick();

        void renderElements();
        void renderElement(GridElement*);

        GridElement::Position* hoveredPosition = nullptr;
        void getHoveredPosition();

        void clickMouse(SDL_Event& e) override;

        void deleteElement() override;

        GridElement::Position lastPosition;
        GridElement::Position* movingPosition = nullptr; 
        void movePosition();

        void UpdateGrid() override;

        float getHoveredLine();
        int getHoveredTrack(); // returns id

        void beginDrop(SDL_DropEvent&) override;
        void renderDrop(SDL_Renderer*) override;
        void dropFile(SDL_DropEvent&) override;        
};

#endif
