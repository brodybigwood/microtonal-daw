#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>
#include "MixerTrack.h"
#include "BinaryData.h"
#include <JuceHeader.h>
#include <iostream>



#ifndef INSTRUMENT_H
#define INSTRUMENT_H

class Project;

class Instrument {

    public:
    Instrument(Project* project);

        ~Instrument();

        std::vector<MixerTrack*> outputs;

        tracktion_engine::Edit* edit;
        tracktion_engine::Engine* engine;

        std::string name = "Instrument Rack";

        //std::vector<int> outputs;
        std::string outputType = "Output to Mixer Tracks:";

        tracktion::engine::Plugin::Ptr plugin;
};

#endif