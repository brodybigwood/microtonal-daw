#include <iostream>
#include <utility>
#include <vector>
#include <climits>
#include "Note.h"
#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <memory>
#include "GridElement.h"

#include <nlohmann/json.hpp>
#include "RhythmLattice.h"
using json = nlohmann::json;

#ifndef REGION_H
#define REGION_H

class Instrument;
class ArrangerNode;

class Region : public GridElement {
    private:
        

    public:
        Region(Project*, ArrangerNode*);
        ~Region() override;

        std::vector<std::shared_ptr<Note>> notes;

    int createNote(std::vector<std::pair<int, int>> startPairs, std::vector<std::pair<int, int>> endPairs, std::vector<std::pair<int, int>> pitchVector = {});
    void deleteNote(int);
    void restoreNoteAt(std::shared_ptr<Note> n, size_t insertIndex);

 void sort();

int releaseMS = 1000;

// Procedural tuning state for PianoRoll (avoids scale object churn).
int tuningMode = 0; // 0=harmonic, 1=edo
int tuningAnchorHarmonic = 1;
// Slot-wise rational lattice offset at the anchor; integer harmonic lines add dense prime exponents on top.
std::vector<std::pair<int, int>> tuningHarmonicAnchorVector;
// Number of equal subdivision steps (EDO line factors).
int tuningEdoSubdivisionSteps = 12;
std::vector<std::pair<int, int>> tuningEdoLowerVector;
std::vector<std::pair<int, int>> tuningEdoUpperVector;
// Rhythm EDO state: equal subdivision of time from lower to upper in seconds.
int rhythmEdoSubdivisionSteps = 1;
std::vector<std::pair<int, int>> rhythmEdoLowerVector; // default empty = time 0
std::vector<std::pair<int, int>> rhythmEdoUpperVector; // default [(1,1)] = 1 second
// The transport strip's line set: a partition of the timeline into {lower, upper, steps}
// spans, the same structure as the single rhythm EDO above. Region is the source of truth,
// so every piano roll on it draws the same set. Mutable from the strip and the grid.
std::vector<RhythmLatticeSpan> transportLines;
// The grid's own rhythm, in the same span structure. Empty means "use the rhythm EDO fields
// above"; the grid dialog writes a single span here, and the button copies transportLines in.
std::vector<RhythmLatticeSpan> gridSpans;

void draw(SDL_Renderer*, float, int) override;
        uint64_t thumbnailSignature() const override;
        void drawThumbnail(SDL_Renderer*, int, int) override;

json toJSON() override;
void fromJSON(json) override;

/** Piano-roll / undo: capture procedural tuning fields only (round-trips with applyTuningUndoFromJSON). */
json tuningUndoToJSON() const;
void applyTuningUndoFromJSON(const json& j);

std::unordered_map<int, int> id_to_index;
idManager id_pool;

};

#endif
