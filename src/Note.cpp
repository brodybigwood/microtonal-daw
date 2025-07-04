#include "Note.h"
#include <SDL3/SDL.h>
#include "fract.h"


Note::Note(fract start, fract end, float num, double temperament) {
    this->end = end;
    this->num = num;
    this->temperament = temperament;
    this->start = start;
    this->midiNum = 0;  // Initialize midiNum if needed (default value, can be changed later)
    this->pitchBend = 0.0f;  // Initialize pitchBend to 0
    this->modX = 0.0f;  // Initialize modulation X
    this->modY = 0.0f;  // Initialize modulation Y
    this->modZ = 0.0f;  // Initialize modulation Z

    for (int i = 0; i < 16; ++i) {
        this->destinationTracks[i] = -1;  // Default track number as -1 or another value to denote "no track"
    }
}

Note::~Note() {
    // Destructor logic, if any is needed in future
}
