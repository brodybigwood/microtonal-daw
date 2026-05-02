
#include <climits>
#include <SDL_ttf.h>
#include <utility>
#include <vector>
#include "GridView.h"
#include "Region.h"
#include "Note.h"
#include "fract.h" 
#include "styles.h"
#include "Project.h"
#include <string>

#ifndef PIANOROLL_H
#define PIANOROLL_H

struct PianoRollPitchLine {
    float midi = 0.f;
    std::vector<std::pair<int, int>> integerPairs{{1, 1}};
    explicit PianoRollPitchLine(float m) : midi(m), integerPairs{{1, 1}} {}
};

class PianoRoll : public GridView {

    public:
    enum class TuningMode {
        Harmonic = 0,
        EDO = 1
    };

    void newTuning();
    void updateLines();
    

    PianoRoll(bool* detached, SDL_FRect*, Region*, Window*);
    ~PianoRoll() override;
    
        SDL_Texture* backgroundTexture;
        SDL_Texture* PianoTexture;
        SDL_Texture* NotesTexture;
        SDL_Texture* KeyTexture;

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

        bool customTick() override;

        void UpdateGrid() override;
        

        void createKeys();
        void RenderRoll();
        void RenderDestinations();
        void RenderNotes();
        double getMidiNum();
        void handleCustomInput(SDL_Event&) override;

        void clickMouse(SDL_Event&) override;

        void initWindow();

        void Scroll();

        void createElement() override;

        void deleteElement() override;

        bool getExistingNote();

        double getNoteName(double);
        float getY(float) override;

        
        fract lastLength = fract(1, 1);

        SDL_Texture* layers[4]; 

        float getHoveredLine();
        void handleMouse();
        std::vector<std::string> lineLabels;
        // Parallel index: harmonic number (harmonic mode) or EDO line index k (EDO mode).
        std::vector<int> lineStructural;

        float getNotePosX(std::shared_ptr<Note>);
        float getNoteEnd(std::shared_ptr<Note>);
        float noteHeight = 5;

        void moveNote(std::shared_ptr<Note>, int, float);

        /** Sync open piano roll UI after region tuning undo/redo; pass noteIdToStamp >= 0 to run stampNoteTuning. */
        static void notifyTuningUndoApplied(Project* p, const std::vector<int>& managerPath, int arrangerNodeId, int regionId,
                                            int noteIdToStamp);

        void handleWindowInput(SDL_Event&) override;
        
    private:
        TuningMode tuningMode = TuningMode::Harmonic;
        float harmonicAnchorMidi = 69.0f;
        int harmonicAnchorNumber = 1;
        float edoAnchorMidi = 69.0f;
        float edoStep = 1.0f;

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
        SDL_FRect modeButtonRect{8.0f, 0.0f, 180.0f, 0.0f};

        size_t closestLineIndexForMidi(float midiPitch) const;
        std::vector<std::pair<int, int>> pitchIntegerPairsAtGridMidi(float midiPitch) const;
        void refreshHoveredPitchLineIndex();
        int hoveredHarmonicFromGrid();
        int hoveredEdoKFromGrid();
        int structuralHarmonicNearNote(const std::shared_ptr<Note>& n);
        int structuralEdoKNearNote(const std::shared_ptr<Note>& n);

        float harmonicToMidi(int harmonic) const;
        void applyHarmonicAnchor(float midi, int harmonic);
        void defineEdoFromInterval(float a, float b, int steps);
        void syncTuningToRegion();
        void loadTuningFromRegion();
        void applyNoteTuning(const std::shared_ptr<Note>& note);
        void stampNoteTuning(const std::shared_ptr<Note>& note);

        bool getStretchingNote();
        void stretchElement(int amount);

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
        void renderPianoRollGridTexture();
};

#endif
