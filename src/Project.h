#include "Note.h"
#include <vector>
#include "Plugin.h"
#include "fract.h"
#include "Instrument.h"
#include <iostream>
#include "MixerTrack.h"


#ifndef PROJECT_H
#define PROJECT_H

#include "EventManager.h"

class Instrument;
class Rack;

namespace DAW {
    class Region;
}


class Project {
    public:
    Project();
        ~Project();

        bool processing = false;

        static Project* instance();

        int sampleTime = 0;

        std::string filepath = "";

        float tempo = 120;

        void load(std::string path = "");

        void save();

        void createRegion();
        void createInstrument();

        fract startTime;

        float mouseX;
        float mouseY;

        void togglePlaying();

        void stop();

        uint16_t id_inst = 0;
        uint16_t id_track = 0;
        uint16_t id_reg = 0;
        uint16_t id_rack = 0;

        std::vector<std::shared_ptr<DAW::Region>> regions;
        std::vector<Instrument*> instruments;
        std::vector<DAW::Region> files;
        std::vector<DAW::Region> recordings;
        std::vector<DAW::Region> automations;

        std::vector<MixerTrack*> tracks;
        std::vector<Rack*> racks;

        void tick();

        void setup();

        fract playHeadStart;
        fract playHeadPos = fract(0,1);

        EventManager em;

        bool isPlaying = false;

        double timeSeconds = 0;

        double effectiveTime = 0;

    private:

};

#endif
