#include "Region.h"
#include "Note.h"
#include "MidiRouter.h"

#ifndef PROJECT_H
#define PROJECT_H

class Project {
    public:
    Project(std::string filepath = "");
        ~Project();

    std::string filepath = "";
    std::vector<Region> regions;

    MidiRouter router;

    float tempo = 120;
    Region midiRegion1;
Region midiRegion2;

void load();

void save();
};

#endif