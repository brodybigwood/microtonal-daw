

#include <JuceHeader.h>

#ifndef MIXERTRACK_H
#define MIXERTRACK_H

class Project;

class MixerTrack {
    public:
        MixerTrack(Project* project);
        ~MixerTrack();
       
        tracktion_engine::Edit* edit;
        tracktion_engine::Engine* engine; 

        tracktion_engine::AudioTrack* track;

        std::string name = "Mixer Track";
};

#endif