#include <string>
#include <vector>
#include "Rack.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

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

        uint16_t id;

        std::vector<Instrument*> childInstruments;
        std::vector<MixerTrack*> childTracks;

        void process(float* outputBuffer, int bufferSize);

        json toJSON();
        void fromJSON(json);
              
};

#endif
