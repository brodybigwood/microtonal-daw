#include <iostream>
#include <vector>
#include "Note.h"
#include "fract.h"
#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>

#ifndef REGION_H
#define REGION_H

namespace DAW {

class Region {
    private:
        

    public:
        Region(fract startTime, float y);
        ~Region();

        std::string name = "MIDI Region FX Rack";

        std::vector<int> outputs;
        std::string outputType = "Instruments";


 std::vector<Note> notes; 

bool updateNoteChannel(Note&);


 void createNote(fract, fract);

 void sort();

 fract startTime;

 float y;

 fract length = fract(4,1);

 bool userSetRight = false;
 bool userSetLeft = false;

 void moveX(fract);
 void moveY(int);
 void resize(bool, fract);





};

}

#endif
