#include "Region.h"
#include "Note.h"
#include "MidiRouter.h"
#include <vector>
#include "fract.h"
#include "Instrument.h"
#include <iostream>
#include "MixerTrack.h"

#ifndef PROJECT_H
#define PROJECT_H

class Instrument;

class Project {
    public:
    Project();
        ~Project();

        std::string filepath = "";

        MidiRouter router;

        float tempo = 120;

        void load(std::string filepath = "");

        void save();

        void createRegion(fract x, int y);

        float mouseX;
        float mouseY;

        void play();

        void stop();

        std::vector<Region*> regions;
        std::vector<Instrument*> instruments;
        std::vector<Region> files;
        std::vector<Region> recordings;
        std::vector<Region> automations;

        std::vector<MixerTrack*> tracks;



        fract playHeadStart;
        fract playHeadPos = fract(0,1);



        bool isPlaying = false;

        void setTime(double);

        double timeSeconds = 0;

    private:

};

#endif
