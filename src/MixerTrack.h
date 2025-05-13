#include <string>

#ifndef MIXERTRACK_H
#define MIXERTRACK_H

class Project;

class MixerTrack {
    public:
        MixerTrack(Project* project);
        ~MixerTrack();
       
        std::string name = "Mixer Track";
};

#endif