#include "Note.h"
#include <vector>
#include "fract.h"
#include <iostream>

#ifndef PROJECT_H
#define PROJECT_H

#include "EventManager.h"
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

        fract startTime;

        float mouseX;
        float mouseY;

        void togglePlaying();

        void stop();

        uint16_t id_reg = 0;

        std::vector<std::shared_ptr<DAW::Region>> regions;

        void tick();

        void setup();

        fract playHeadStart;
        fract playHeadPos = fract(0,1);

        EventManager em;

        bool isPlaying = false;

        double timeSeconds = 0;

        double effectiveTime = 0;

        void process(void* input, void* output, int bufferSize);

    private:

};

#endif
