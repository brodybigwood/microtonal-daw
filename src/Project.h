#include "Note.h"
#include <vector>
#include "fract.h"
#include <iostream>

#ifndef PROJECT_H
#define PROJECT_H

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

        bool isPlaying = false;

        double timeSeconds = 0;

        double effectiveTime = 0;

        void process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate);

    private:

};

#endif
