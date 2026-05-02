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
class PianoRoll;

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
        void syncLayout();

        SDL_Texture* texture;
        SDL_Texture* regionTexture;
        SDL_Texture* playHeadTexture;

        void clearTextures() override;
        void generateTextures() override;

        SDL_FRect regionRect;
        SDL_FRect rightRect;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
        void handleCustomInput(SDL_Event& e) override;

        Uint32 lastLmbTime;
        void doubleClick();

        void renderElements();
        void renderElement(GridElement*);

        GridElement::Position* hoveredPosition = nullptr;
        void getHoveredPosition();

        void clickMouse(SDL_Event& e) override;

        enum class PositionDragKind { None = 0, Move, ResizeLeft, ResizeRight };
        PositionDragKind positionDragKind = PositionDragKind::None;

        GridElement::Position lastPosition;
        GridElement::Position* movingPosition = nullptr; 
        void movePosition();

        void UpdateGrid() override;

        float getHoveredLine();
        int getHoveredTrack(); // returns id

        void beginDrop(SDL_DropEvent&) override;
        void renderDrop(SDL_Renderer*) override;
        void dropFile(SDL_DropEvent&) override;        
        
        SDL_FRect pianoRollRect = {200,200,800,600};
        bool pianoRollDetached = true;
        PianoRoll* pianoRoll = nullptr;
        void createPianoRoll(Region*);

    private:
        /** When a piano roll is open, the region id it belongs to (for undo safety after the region is removed). */
        int pianoRollTrackedRegionId = -1;
        int timelineHoverElementId = -1;
        int timelineHoverPositionId = -1;
        int timelineDragElementId = -1;
        int timelineDragPositionId = -1;

        void validateTimelinePointers();
        void clearPianoRoll();
};

#endif
