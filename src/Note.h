#ifndef NOTE_H
#define NOTE_H
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
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

        void move(fract x, fract y);

        json toJSON();
        static std::shared_ptr<Note> fromJSON(json&);
};

#endif
