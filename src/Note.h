#ifndef NOTE_H
#define NOTE_H
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include "fract.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class Note {
    public:
        Note(fract start, fract end, float num);
        ~Note();

        float num;  // Frequency or pitch of the note
        fract start; //bars
        fract end;

        int id;
        int channel;
        int harmonicNumber = 0;
        int tuningMode = 0; // 0=harmonic, 1=edo
        float tuningAnchorMidi = 69.0f;
        int tuningAnchorHarmonic = 1;
        float tuningEdoAnchorMidi = 69.0f;
        float tuningEdoStep = 1.0f;
        // Rational prime-power factors for pitch (same meaning as PianoRollPitchLine::integerPairs).
        // num = 69 + 12*log2(product of primes[i]^(num/den)); empty vector => product 1 => num 69.
        std::vector<std::pair<int, int>> pitchIntegerPairs;

        /** Same mapping as syncNumFromPitchIntegerPairs → num (69 + 12·log₂ prime product). */
        static float midiFromPitchIntegerPairs(const std::vector<std::pair<int, int>>& pairs);
        void syncNumFromPitchIntegerPairs();
        void move(fract x, fract y);

        json toJSON();
        static std::shared_ptr<Note> fromJSON(json&);
        /** Apply start/end/pitch/tuning from a snapshot (e.g. undo); keeps existing id. */
        void applyUndoSnapshot(const json& j);

        /** Subset used by assign-harmonic undo (no start/end/pairs). */
        json tuningFieldsUndoToJSON() const;
        void applyTuningFieldsUndoFromJSON(const json& j);
};

#endif
