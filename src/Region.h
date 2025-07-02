#include <iostream>
#include <vector>
#include "Note.h"
#include "fract.h"
#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>

#ifndef REGION_H
#define REGION_H

class Instrument;

namespace DAW {

class Region {
    private:
        

    public:
        Region(fract startTime, Instrument* inst);
        ~Region();

        std::string name = "MIDI Region FX Rack";

 std::vector<Note> notes; 

bool updateNoteChannel(Note&);


 void createNote(fract, fract);

 void sort();

 fract startTime;

Instrument* inst;

int& index;

int id;

 fract length = fract(4,1);

 bool userSetRight = false;
 bool userSetLeft = false;

 void moveX(fract);
 void resize(bool, fract);

int releaseMS = 1000;



};

}

#endif
