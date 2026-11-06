#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <cmath>
#include <vector>
#include <utility>
#include <unordered_map>
#include <set>
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
class CurveEditor;

class SongRoll : public GridView{

    public:
    SongRoll(SDL_FRect* rect, Window*, Project*, ArrangerNode*);
        ~SongRoll() override;

        ArrangerNode* parentNode;
        TrackManager* tracks;
        ElementManager* em;
        /** Re-point at the node's current managers (they are replaced on project load). */
        void bindNode(ArrangerNode* n);
        SDL_FRect leftRect;

        WindowHandler* windowHandler;

        void renderMargins(SDL_Renderer* renderer);
        void createElement() override;

        float getY(float) override;

        bool customTick(SDL_Renderer* renderer) override;
        void syncLayout();
        bool showRecordButtons() const override { return true; }

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
        bool handleKeyboard(SDL_Event& e);

        // Position selection (arranger-level clipboard works like the piano roll's notes).
        std::set<int> selectedPositionIds;
        bool positionSelected(int id) const { return selectedPositionIds.count(id) > 0; }
        bool selectingPositionBand = false;
        float bandStartX = 0, bandStartY = 0, bandEndX = 0, bandEndY = 0;

        struct PositionDragSnapshot {
            int elementID = 0;
            int positionID = 0;
            uint16_t trackID = 0;
            std::vector<std::pair<int,int>> rhythmVector;
            std::vector<std::pair<int,int>> rhythmEndVector;
            std::vector<std::pair<int,int>> startOffsetPairs;
            json before;
        };
        bool movingMultiplePositions = false;
        std::vector<PositionDragSnapshot> multiMoveSnapshots;

        GridElement::Position* findPosition(int elementID, int positionID, GridElement** elementOut = nullptr);
        void copySelectedPositions();
        void pastePositionClipboard(float firstBeat, bool exactDuplicate = false);
        void deleteSelectedPositions();

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
        void closePianoRollWindow(PianoRollWindow* prw, bool createUndo);

        // Rhythm grid — same model as PianoRoll
        void RenderGridTexture(SDL_Renderer* renderer) override;
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

        // Ctrl+drag on the transport strip: subdivide only between two picked transport lines.
        // The lines live on ArrangerNode::transportLines so every view of this arranger shares them.
        bool selectingTransportLines = false;
        bool transportLineDragExtend = false;
        bool transportLineDragMoved = false;
        std::vector<std::pair<int, int>> transportLineDragStartPairs;
        std::vector<std::pair<int, int>> transportLineDragEndPairs;
        size_t hoveredTransportLineIndex = SIZE_MAX;
        void updateTransportLines() override;
        void refreshHoveredTransportLineIndex();
        size_t closestTransportLineIndexForSeconds(float seconds);
        void subdivideBetweenRhythmLines(const std::vector<std::pair<int, int>>& aPairs,
                                         const std::vector<std::pair<int, int>>& bPairs,
                                         int parts);
        void extendTransportLines(const std::vector<std::pair<int, int>>& cursorPairs);
        void applyTransportToGrid() override;
        json arrangerRhythmEdoJson() const;
        void commitArrangerRhythmEdo(const json& before, const std::string& label);
        /** Copy a position's stored rhythm-Edo span into the arranger grid (undoable). */
        void recallPositionRhythm(const GridElement::Position* pos);

        // Drag state for position snapping
        std::vector<std::pair<int, int>> positionDragStartPairs;
        std::vector<std::pair<int, int>> positionDragEndPairs;
        float positionDragGrabOffsetPx = 0.f;

        // Last created position state, for consecutive placements
        std::vector<std::pair<int, int>> lastPositionDurationPairs;
        std::vector<std::pair<int, int>> lastPositionStartOffsetPairs;

    // Curve editor state (for automation curve positions)
    std::unordered_map<int, CurveEditor*> curveEditors;
    int hoveredCurvePointIdx = -1;
    int draggingCurvePointIdx = -1;
    int draggingTensionSeg = -1;
    float tensionDragStartY = 0.f;
    float tensionDragStartParam = 0.f;
    int activeCurveEditorPosId = -1;
    json curveDragUndoBefore;
    CurveEditor* getCurveEditor(GridElement::Position* pos);
    void destroyCurveEditor(int posId);
    json snapshotCurvePoints(AutomationCurve* ac);
    void pushCurveUndo(AutomationCurve* ac, const json& before);

    private:
        int timelineHoverElementId = -1;
        int timelineHoverPositionId = -1;
        int timelineDragElementId = -1;
        int timelineDragPositionId = -1;

        void validateTimelinePointers();

};

#endif
