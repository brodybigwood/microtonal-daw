
#include <climits>
#include <SDL_ttf.h>
#include <utility>
#include <vector>
#include <set>
#include <map>
#include "GridView.h"
#include "EmbeddedWindow.h"
#include "Region.h"
#include "Note.h"
#include "fract.h"
#include "styles.h"
#include "Project.h"
#include <optional>
#include <string>

#ifndef PIANOROLL_H
#define PIANOROLL_H

struct PianoRollPitchLine {
    float midi = 0.f;
    std::vector<std::pair<int, int>> integerPairs{{1, 1}};
    explicit PianoRollPitchLine(float m) : midi(m), integerPairs{{1, 1}} {}
};

struct PianoRollRhythmLine {
    float seconds = 0.f;
    std::vector<std::pair<int, int>> integerPairs;
    bool isBeat = false;
    explicit PianoRollRhythmLine(float s) : seconds(s) {}
};

class PianoRoll : public GridView, public EmbeddedWindow {

    public:
    enum class TuningMode {
        Harmonic = 0,
        EDO = 1
    };

    void newTuning();
    void updateLines();


    PianoRoll(Region*, Window* parent);
    ~PianoRoll() override;

        // EmbeddedWindow interface
        void renderContent(SDL_Renderer* r) override;
        bool handleInput(SDL_Event& e) override;
        bool handleContentInput(SDL_Event& e) override;

        SDL_Texture* backgroundTexture = nullptr;
        SDL_Texture* PianoTexture = nullptr;
        SDL_Texture* NotesTexture = nullptr;
        SDL_Texture* KeyTexture = nullptr;

        SDL_FRect textRect;

        SDL_Texture* keys[128];

        Region* region;
        double referenceOffset;

        double notesPerOctave = 12;
        double cellHeight12;

        double yOffset12;

        double numCellsDown12;

        double numCellsRight;
        double numCellsDown;

        double yMax;

        std::shared_ptr<Note> movingNote;
        json movingNoteUndoBefore;
        bool movingNoteHasUndoSnapshot = false;
        bool movingNoteDragDirty = false;
        std::optional<float> movingNotePitchPreviewLineMidi;
        std::optional<size_t> movingNoteRhythmPreviewLineIdx;
        json stretchingNoteUndoBefore;
        bool stretchingNoteHasUndoSnapshot = false;
        bool stretchingNoteDragDirty = false;

        // Multi-note selection
        std::set<int> selectedNoteIds;
        bool selectingRubberBand = false;
        float rubberBandStartX = 0, rubberBandStartY = 0;
        float rubberBandEndX = 0, rubberBandEndY = 0;

        // Multi-note move state (populated when dragging a selected note)
        bool movingMultipleNotes = false;
        std::map<int, json> multiMoveBefores;                // noteId -> JSON snapshot at mousedown
        std::map<int, std::vector<std::pair<int,int>>> multiPitchPreviews; // noteId -> preview integerPairs
        std::vector<std::pair<int,int>> dragStartPairs;      // pitch integerPairs at drag start
        std::vector<std::pair<int,int>> rhythmDragStartPairs; // rhythm pairs at drag start

        // Multi-note resize state
        bool stretchingMultipleNotes = false;
        std::map<int, json> multiStretchBefores;             // noteId -> JSON snapshot at mousedown

        bool customTick(SDL_Renderer* renderer) override;

        void UpdateGrid() override;


        void createKeys();
        void RenderRoll();
        void RenderDestinations(SDL_Renderer* renderer);
        void RenderNotes(SDL_Renderer* renderer);
        double getMidiNum();
        void handleCustomInput(SDL_Event&) override;

        void clickMouse(SDL_Event&) override;

        void initWindow(SDL_Renderer* renderer);

        void Scroll();

        void createElement() override;

        void deleteElement() override;
        void deleteSelection();

        bool getExistingNote();

        double getNoteName(double);
        float getY(float) override;


        fract lastLength = fract(1, 1);

        SDL_Texture* layers[4];

        float getHoveredLine();
        void handleMouse();
        std::vector<std::string> lineLabels;
        std::vector<int> lineStructural;
        std::vector<SDL_Texture*> lineLabelTextures; // cached once in updateLines()

        float getNotePosX(std::shared_ptr<Note>);
        float getNoteEnd(std::shared_ptr<Note>);
        float noteHeight = 5;

        static void notifyTuningUndoApplied(Project* p, const std::vector<int>& managerPath, int arrangerNodeId, int regionId,
                                            int noteIdToStamp);
        
    private:
        TuningMode tuningMode = TuningMode::Harmonic;
        int harmonicAnchorNumber = 1;

        bool selectingInterval = false;
        float intervalStartLine = 0.0f;
        float intervalEndLine = 0.0f;
        std::shared_ptr<Note> intervalStartNote = nullptr;
        bool intervalDragMoved = false;
        // True iff shift-interval drag began in harmonic mode (structural endpoints are harmonics).
        bool intervalSelectStartedHarmonic = false;
        int intervalDragHarmA = 0;
        int intervalDragHarmB = 0;
        int intervalDragEdoKA = INT_MAX;
        int intervalDragEdoKB = INT_MAX;
        /** Rational snapshot at mousedown / while hovering end note; avoids pitchIntegerPairsAtGridMidi(note->num). */
        std::vector<std::pair<int, int>> intervalDragStartVertexPairs;
        std::vector<std::pair<int, int>> intervalDragEndVertexPairs;
        /** Define-EDO text dialog: keep band + commit vectors frozen from last drag frame until dialog closes. */
        bool intervalEdoDefineDialogOpen = false;
        float intervalDialogFrozenStartLine = 0.0f;
        float intervalDialogFrozenEndLine = 0.0f;
        std::vector<std::pair<int, int>> intervalDialogFrozenStartVertexPairs;
        std::vector<std::pair<int, int>> intervalDialogFrozenEndVertexPairs;
        SDL_FRect modeButtonRect{8.0f, 0.0f, 180.0f, 0.0f};

        // Rhythm interval drag (Ctrl+Shift)
        bool selectingRhythmInterval = false;
        std::shared_ptr<Note> rhythmIntervalStartNote = nullptr;
        float rhythmIntervalStartSec = 0.0f;
        float rhythmIntervalEndSec = 0.0f;
        std::vector<std::pair<int, int>> rhythmDragStartVertexPairs;
        std::vector<std::pair<int, int>> rhythmDragEndVertexPairs;
        bool rhythmIntervalDragMoved = false;
        bool rhythmEdoDefineDialogOpen = false;
        float rhythmDialogFrozenStartSec = 0.0f;
        float rhythmDialogFrozenEndSec = 0.0f;
        std::vector<std::pair<int, int>> rhythmDialogFrozenStartPairs;
        std::vector<std::pair<int, int>> rhythmDialogFrozenEndPairs;

        size_t closestLineIndexForMidi(float midiPitch) const;
        std::vector<std::pair<int, int>> pitchIntegerPairsAtGridMidi(float midiPitch) const;
        void refreshHoveredPitchLineIndex();
        int hoveredHarmonicFromGrid();
        int hoveredEdoKFromGrid();
        int structuralHarmonicNearNote(const std::shared_ptr<Note>& n);
        int structuralEdoKNearNote(const std::shared_ptr<Note>& n);


        void syncTuningToRegion();
        void loadTuningFromRegion();
        void applyNoteTuning(const std::shared_ptr<Note>& note);
        void stampNoteTuning(const std::shared_ptr<Note>& note);

        bool getStretchingNote();
        void stretchElement(int amount);
        void moveNoteTime(std::shared_ptr<Note> note);
        void commitNotePitchSnap(std::shared_ptr<Note> note, float targetLineMidi);
        void snapNoteRhythm(const std::shared_ptr<Note>& note);
        float noteMidiForRender(const std::shared_ptr<Note>& note) const;

        std::shared_ptr<Note> hoveredElement;

        float selectThresholdX = 5.0f;
        float selectThresholdY = 5.0f;

        std::shared_ptr<Note> stretchingNote = nullptr;
        int resizeDir;
        bool isStretchingNote = false;

        float noteRadius = 5;

        int hoverPitchFactorsNoteId = -1;
        Uint64 hoverPitchFactorsStartMs = 0;

        void refreshPitchFactorsHoverTiming();
        void renderPitchFactorsHoverTooltip();

        // Discrete pitch line under cursor (UI → int); scale logic reads this, not mouseY/num.
        size_t hoveredPitchLineIndex = SIZE_MAX;

        std::vector<PianoRollPitchLine> pitchLines;
        std::vector<PianoRollRhythmLine> rhythmLines;
        std::vector<std::string> rhythmLineLabels;
        size_t hoveredRhythmLineIndex = SIZE_MAX;
        void updateRhythmLines();
        void refreshHoveredRhythmLineIndex();
        size_t closestRhythmLineIndexForSeconds(float seconds);
        void renderPianoRollGridTexture(SDL_Renderer* renderer);

    public:
        bool needsInit_ = false;
};

#endif
