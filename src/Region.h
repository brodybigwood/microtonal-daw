#include <iostream>
#include <vector>
#include "Note.h"
#include "fract.h"
#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <memory>
#include "GridElement.h"

#ifndef REGION_H
#define REGION_H

class Instrument;

namespace DAW {

class Region : GridElement {
    private:
        

    public:
        Region(fract startTime, Instrument* inst);
        ~Region();

        std::string name = "MIDI Region FX Rack";

 std::vector<std::shared_ptr<Note>> notes;

bool updateNoteChannel(std::shared_ptr<Note>);


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
