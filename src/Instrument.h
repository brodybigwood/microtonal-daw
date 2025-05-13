#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include "MixerTrack.h"
#include <iostream>
#include "Rack.h"
#include "Plugin.h"

#ifndef INSTRUMENT_H
#define INSTRUMENT_H

class Project;

class Instrument {

    public:
    Instrument(Project* project);

        ~Instrument();

        std::vector<MixerTrack*> outputs;

        Project* project;

        std::string name = "Instrument Rack";

        //std::vector<int> outputs;
        std::string outputType = "Output to Mixer Tracks:";

        Rack rack;

        void process(float* outputBuffer, int bufferSize);

        void addDestination(int trackIndex);

};

#endif