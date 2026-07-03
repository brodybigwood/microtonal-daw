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
        Note(const std::vector<std::pair<int, int>>& rhythmPairs, float durationSeconds, float num);
        ~Note();

        float num;  // Frequency or pitch of the note
        float durationSeconds = 1.0f;

        int id;
        int channel;
        int tuningMode = 0; // 0=harmonic, 1=edo
        int tuningAnchorHarmonic = 1;
        int tuningEdoSubdivisionSteps = 12;
        std::vector<std::pair<int, int>> tuningEdoLowerVector;
        std::vector<std::pair<int, int>> tuningEdoUpperVector;
        // Rhythm: start time from t=0 as product of primes^(num/den) seconds.
        std::vector<std::pair<int, int>> rhythmIntegerPairs;
        int rhythmEdoSubdivisionSteps = 1;
        std::vector<std::pair<int, int>> rhythmEdoLowerVector;
        std::vector<std::pair<int, int>> rhythmEdoUpperVector;
        // Rational prime-power factors for pitch (same meaning as PianoRollPitchLine::integerPairs).
        // num = 69 + 12*log2(product of primes[i]^(num/den)); empty vector => product 1 => num 69.
        std::vector<std::pair<int, int>> pitchIntegerPairs;

        /** Same mapping as syncNumFromPitchIntegerPairs → num (69 + 12·log₂ prime product). */
        static float midiFromPitchIntegerPairs(const std::vector<std::pair<int, int>>& pairs);
        static float secondsFromIntegerPairs(const std::vector<std::pair<int, int>>& pairs);
        void syncNumFromPitchIntegerPairs();

        float startSeconds() const;
        float endSeconds() const;

        json toJSON();
        static std::shared_ptr<Note> fromJSON(json&);
        /** Apply start/end/pitch/tuning from a snapshot (e.g. undo); keeps existing id. */
        void applyUndoSnapshot(const json& j);

        /** Subset used by assign-harmonic undo (no start/end/pairs). */
        json tuningFieldsUndoToJSON() const;
        void applyTuningFieldsUndoFromJSON(const json& j);
};

#endif
