#include "Note.h"
#include <vector>
#include "fract.h"
#include <iostream>
#include "UndoManager.h"

#ifndef PROJECT_H
#define PROJECT_H

namespace DAW {

    class Region;
}

class NodeManager;
class NodeEditor;
class TrackManager;
class ElementManager;

class Project {
    public:
    Project();
        ~Project();

        bool processing = false;

        static Project* instance();
        NodeManager* nm;
        NodeEditor* ne;
        TrackManager* tm;
        ElementManager* em;        
        ScaleManager* sm;

        int sampleTime = 0;

        int beatsToSamples(float);

        std::string filepath = "";

        float tempo = 120;

        void load(std::string path = "");

        void save();

        void createRegion();
        void createNote(fract, fract, float, TuningTable*, int);

        fract startTime;

        float mouseX;
        float mouseY;

        void togglePlaying();

        void stop();

        uint16_t id_reg = 0;

        void tick();

        void setup();

        fract playHeadStart;
        fract playHeadPos = fract(0,1);

        bool isPlaying = false;

        double timeSeconds = 0;

        double effectiveTime = 0;

        void process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate);

        UndoManager* um;
        void undo() { um->undo(); }
        void redo() { um->redo(); }

    private:

};

#endif
