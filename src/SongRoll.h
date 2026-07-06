#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <cmath>
#include <vector>
#include <utility>
#include "GridElement.h"
#include "GridView.h"
#include "styles.h"
#include "Project.h"
#include <iostream>
#include "WindowHandler.h"
#include "Playhead.h"
#include "PianoRollInternal.h"

#include "TrackManager.h"

#ifndef SONGROLL_H
#define SONGROLL_H

class WindowHandler;  // forward declaration
class ElementManager;
class ArrangerNode;
class PianoRoll;
class PianoRollWindow;

class SongRoll : public GridView{

    public:
    SongRoll(SDL_FRect* rect, Window*, Project*, ArrangerNode*);
        ~SongRoll() override;

        ArrangerNode* parentNode;
        TrackManager* tracks;
        ElementManager* em;
        SDL_FRect leftRect;

        WindowHandler* windowHandler;

        void renderMargins(SDL_Renderer* renderer);
        void createElement() override;

        float getY(float) override;

        bool customTick(SDL_Renderer* renderer) override;
        void syncLayout();

        SDL_Texture* texture = nullptr;
        SDL_Texture* regionTexture = nullptr;
        SDL_Texture* playHeadTexture = nullptr;

        void clearTextures() override;
        void generateTextures(SDL_Renderer* renderer) override;

        SDL_FRect regionRect;
        SDL_FRect rightRect;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
        void handleCustomInput(SDL_Event& e) override;

        Uint32 lastLmbTime;
        void doubleClick();

        void renderElements(SDL_Renderer* renderer);
        void renderElement(SDL_Renderer* renderer, GridElement*);

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

        std::vector<PianoRoll*> pianoRolls;
        std::vector<PianoRollWindow*> pianoRollWindows;
        void createPianoRoll(Region*, bool createUndo = true, int forceEwID = -1);
        void clearPianoRoll(int regionId, bool createUndo = true);

        // Rhythm grid — same model as PianoRoll
        void RenderGridTexture(SDL_Renderer* renderer) override;
        std::vector<RhythmGridLine> rhythmLines;
        std::vector<std::string> rhythmLineLabels;
        size_t hoveredRhythmLineIndex = SIZE_MAX;
        void updateRhythmLines();
        void refreshHoveredRhythmLineIndex();
        float secondsFromMouseX();
        std::vector<std::pair<int, int>> pairsAtMouseX();

        // Ctrl+Shift rhythm interval drag
        bool selectingRhythmInterval = false;
        float rhythmIntervalStartSec = 0.0f;
        float rhythmIntervalEndSec = 0.0f;
        bool rhythmIntervalDragMoved = false;
        std::vector<std::pair<int, int>> rhythmDragStartVertexPairs;
        std::vector<std::pair<int, int>> rhythmDragEndVertexPairs;
        bool rhythmEdoDefineDialogOpen = false;
        float rhythmDialogFrozenStartSec = 0.0f;
        float rhythmDialogFrozenEndSec = 0.0f;
        std::vector<std::pair<int, int>> rhythmDialogFrozenStartPairs;
        std::vector<std::pair<int, int>> rhythmDialogFrozenEndPairs;

        // Drag state for position snapping
        std::vector<std::pair<int, int>> positionDragStartPairs;
        std::vector<std::pair<int, int>> positionDragEndPairs;
        float positionDragGrabOffsetPx = 0.f;

        // Last created position duration, for consecutive placements
        std::vector<std::pair<int, int>> lastPositionDurationPairs;

    private:
        int timelineHoverElementId = -1;
        int timelineHoverPositionId = -1;
        int timelineDragElementId = -1;
        int timelineDragPositionId = -1;

        void validateTimelinePointers();

};

#endif
