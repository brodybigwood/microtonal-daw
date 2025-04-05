#ifndef NOTE_H
#define NOTE_H

#include <SDL3/SDL.h>

#include "fract.h"

class Note {
    public:
        Note(fract start, fract end, fract num, double temperament);
        ~Note();

        fract num;  // Frequency or pitch of the note
        fract start;
        fract end;

        int midiNum, pitchBend, modX, modY, modZ;  // Modulation parameters

        double temperament;

        int destinationTracks[16];  // Array for destination tracks (might be useful in your design)

        void play();  
        
        void move(fract x, fract y);
};

#endif
