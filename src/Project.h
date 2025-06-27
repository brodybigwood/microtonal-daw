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

#include "EventManager.h"

class Instrument;

class Project {
    public:
    Project();
        ~Project();

        static Project* instance();

        int sampleTime = 0;

        std::string filepath = "";

        MidiRouter router;

        float tempo = 120;

        void load(std::string filepath = "");

        void save();

        void createRegion(fract x, int y);

        float mouseX;
        float mouseY;

        void play();

        void togglePlaying();

        void stop();

        std::vector<Region*> regions;
        std::vector<Instrument*> instruments;
        std::vector<Region> files;
        std::vector<Region> recordings;
        std::vector<Region> automations;

        std::vector<MixerTrack*> tracks;

        void tick();

        void setup();

        fract playHeadStart;
        fract playHeadPos = fract(0,1);

        EventManager em;

        bool isPlaying = false;

        void setTime(double);

        double timeSeconds = 0;

    private:

};

#endif
