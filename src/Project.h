#include "Region.h"
#include "Note.h"
#include "MidiRouter.h"
#include <vector>
#include "fract.h"
#include <JuceHeader.h>
#include "Instrument.h"
#include <iostream>

#ifndef PROJECT_H
#define PROJECT_H

class Project {
    public:
    Project(std::string filepath = "");
        ~Project();

    std::string filepath = "";


    MidiRouter router;

    float tempo = 120;

void load();

void save();

void createRegion(fract x, int y);

float mouseX;
float mouseY;

void play();

void stop();

std::vector<Region> regions;
std::vector<Instrument> instruments;
std::vector<Region> files;
std::vector<Region> recordings;
std::vector<Region> automations;

struct element {
    std::string type;
    int index;
};

element* viewedElement;

void setViewedElement(std::string type, int index);

fract playHeadStart;
fract playHeadPos = fract(0,1);


tracktion_engine::Engine engine { ProjectInfo::projectName };



tracktion_engine::Edit* edit;

enum class PlayState {
    Play,
    Stop
};

PlayState playState { PlayState::Stop };

void setTime(double);

private:

};

#endif