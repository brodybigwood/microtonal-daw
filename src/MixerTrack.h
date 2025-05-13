#include <string>
#include <vector>
#include "Rack.h"

#ifndef MIXERTRACK_H
#define MIXERTRACK_H

class Project;
class Instrument;

class MixerTrack {
    public:
        MixerTrack(Project* project);
        ~MixerTrack();
       
        std::string name = "Mixer Track";

        Rack rack;



        std::vector<Instrument*> childInstruments;
        std::vector<MixerTrack*> childTracks;

        void process(float* outputBuffer, int bufferSize);
              
};

#endif