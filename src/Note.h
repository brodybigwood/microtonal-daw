#ifndef NOTE_H
#define NOTE_H

#include <SDL3/SDL.h>

#include "fract.h"

class Note {
    public:
        Note(fract start, fract end, float num, double temperament);
        ~Note();

        float num;  // Frequency or pitch of the note
        fract start; //bars
        fract end;

        int id;
        int channel;

        SDL_Color customColor;

        int midiNum, pitchBend, modX, modY, modZ;  // Modulation parameters

        double temperament;

        int destinationTracks[16];  // Array for destination tracks (might be useful in your design)
        
        void move(fract x, fract y);
};

#endif
