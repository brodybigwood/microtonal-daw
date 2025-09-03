#include <string>
#include <vector>
#include "Rack.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include "Processing.h"
#include "VolumeBar.h"

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

        void process(audioData);

        json toJSON();
        void fromJSON(json);

        void render(SDL_Renderer*, SDL_FRect*);
        VolumeBar bar;
};

#endif
