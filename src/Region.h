#include <iostream>
#include <utility>
#include <vector>
#include <climits>
#include "Note.h"
#include "fract.h"
#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <memory>
#include "GridElement.h"

#include <nlohmann/json.hpp>
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

        std::string name = "MIDI Region FX Rack";
        std::vector<std::shared_ptr<Note>> notes;

    int createNote(fract, fract, float, std::vector<std::pair<int, int>> pitchIntegerPairs = {});
    void deleteNote(int);
    void restoreNoteAt(std::shared_ptr<Note> n, size_t insertIndex);

 void sort();

int releaseMS = 1000;

// Procedural tuning state for PianoRoll (avoids scale object churn).
int tuningMode = 0; // 0=harmonic, 1=edo
float tuningAnchorMidi = 69.0f;
int tuningAnchorHarmonic = 1;
// Slot-wise rational lattice offset at the anchor; integer harmonic lines add dense prime exponents on top.
std::vector<std::pair<int, int>> tuningHarmonicAnchorVector;
float tuningEdoAnchorMidi = 69.0f;
float tuningEdoStep = 1.0f;
// When >0, subdividing [tuningEdoSpanLoMidi, tuningEdoSpanHiMidi] into this many parts (grid / UI).
int tuningEdoSpanDivisions = 0;
float tuningEdoSpanLoMidi = 0.0f;
float tuningEdoSpanHiMidi = 0.0f;

int tuningSpanLoHarm = 0;
int tuningSpanHiHarm = 0;
int tuningSpanLoEdoK = INT_MAX;
int tuningSpanHiEdoK = INT_MAX;
int tuningEdoStepSemiNum = 1;
int tuningEdoStepSemiDen = 1;
// Equal subdivision of rational pitch vector from lower to upper into this many steps (EDO line factors).
int tuningEdoSubdivisionSteps = 12;
std::vector<std::pair<int, int>> tuningEdoLowerVector;
std::vector<std::pair<int, int>> tuningEdoUpperVector;

void draw(SDL_Renderer*, float, int) override;

json toJSON() override;
void fromJSON(json) override;

/** Piano-roll / undo: capture procedural tuning fields only (round-trips with applyTuningUndoFromJSON). */
json tuningUndoToJSON() const;
void applyTuningUndoFromJSON(const json& j);

std::unordered_map<int, int> id_to_index;
idManager id_pool;

};

#endif
