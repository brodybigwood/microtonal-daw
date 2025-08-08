#include <iostream>
#include <vector>
#include "Note.h"
#include "fract.h"
#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <memory>
#include "GridElement.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#ifndef REGION_H
#define REGION_H

class Instrument;

namespace DAW {

class Region : public GridElement {
    private:
        

    public:
        Region();
        ~Region();

        std::string name = "MIDI Region FX Rack";

 std::vector<std::shared_ptr<Note>> notes;

 uint16_t id;

bool updateNoteChannel(std::shared_ptr<Note>);


 void createNote(fract, fract);

 void sort();

 void moveX(fract);
 void resize(bool, fract);

int releaseMS = 1000;

void draw(SDL_Renderer*) override;

json toJSON();
void fromJSON(json);

};

}

#endif
