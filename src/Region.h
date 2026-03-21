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
class TuningTable;
class ScaleManager;
class ArrangerNode;

class Region : public GridElement {
    private:
        

    public:
        Region(Project*, ScaleManager*, ArrangerNode*);
        ~Region() override;

ScaleManager* sm;
        std::string name = "MIDI Region FX Rack";
TuningTable* scale;
TuningTable* getTuning();
 std::vector<std::shared_ptr<Note>> notes;

    int createNote(fract, fract, float, TuningTable*);
    void deleteNote(int);

 void sort();

int releaseMS = 1000;

void draw(SDL_Renderer*, float, int) override;

json toJSON() override;
void fromJSON(json) override;

std::unordered_map<int, int> id_to_index;
idManager id_pool;

};

#endif
