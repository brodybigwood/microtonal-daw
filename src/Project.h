#include "Region.h"
#include "Note.h"
#include "MidiRouter.h"
#include <vector>
#include "fract.h"
#include <JuceHeader.h>

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

void load();

void save();

void createRegion(fract x, int y);

float mouseX;
float mouseY;

void play();

void stop();

fract lastTime = fract(0,1);

private:
    tracktion_engine::Engine engine { ProjectInfo::projectName };

    tracktion_engine::Edit* edit;

    enum class PlayState {
        Play,
        Stop
    };

    PlayState playState { PlayState::Stop };

};

#endif