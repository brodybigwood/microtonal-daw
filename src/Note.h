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
        Note(const std::vector<std::pair<int, int>>& startPairs, const std::vector<std::pair<int, int>>& endPairs);
        ~Note();

        float num;  // Frequency or pitch of the note

        int id;
        int channel = 0;
        int tuningMode = 0; // 0=harmonic, 1=edo
        int tuningAnchorHarmonic = 1;
        int tuningEdoSubdivisionSteps = 12;
        std::vector<std::pair<int, int>> tuningEdoLowerVector;
        std::vector<std::pair<int, int>> tuningEdoUpperVector;
        // Rhythm: start and end time from t=0 as product of primes^(num/den) seconds.
        std::vector<std::pair<int, int>> rhythmVector;
        std::vector<std::pair<int, int>> rhythmEndVector;
        int rhythmEdoSubdivisionSteps = 1;
        std::vector<std::pair<int, int>> rhythmEdoLowerVector;
        std::vector<std::pair<int, int>> rhythmEdoUpperVector;
        // Rational prime-power factors for pitch (same meaning as PianoRollPitchLine::integerPairs).
        // num = 69 + 12*log2(product of primes[i]^(num/den)); empty vector => product 1 => num 69.
        std::vector<std::pair<int, int>> pitchVector;

        /** Same mapping as syncNumFromPitchVector → num (69 + 12·log₂ prime product). */
        static float midiFromPitchVector(const std::vector<std::pair<int, int>>& pairs);
        static float secondsFromVector(const std::vector<std::pair<int, int>>& pairs);
        void syncNumFromPitchVector();

        float startSeconds() const;
        float endSeconds() const;
        float durationSeconds() const;

        json toJSON();
        static std::shared_ptr<Note> fromJSON(json&);
        /** Apply start/end/pitch/tuning from a snapshot (e.g. undo); keeps existing id. */
        void applyUndoSnapshot(const json& j);

        /** Subset used by assign-harmonic undo (no start/end/pairs). */
        json tuningFieldsUndoToJSON() const;
        void applyTuningFieldsUndoFromJSON(const json& j);
};

#endif
