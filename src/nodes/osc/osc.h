#pragma once

#include "Node.h"
#define NUM_VOICES 32
#define MAX_ADSR 30 // 30 seconds

struct Voice {
    Voice();
   ~Voice();
    void update();

    bool active = false;
    float frequency;
    void reset();
    int noteId = -1;
    float phase = 0;
    int wait_on = 0;
    int wait_off = -1;
    void process (float* out0, float* out1, int& bufferSize, int& sampleRate); //stereo out

    int samplesPassed = 0; // how many samples have passed since activation

    // adsr in seconds
    float attack = 0.25;
    float decay = 2;
    float sustain = 0.5; // sustain level out of 1.0
    float release = 0.5;

    float* adsr = nullptr;
    void updateADSR();
};


class OscillatorNode : public Node {
    public:
        OscillatorNode(uint16_t);


        Connection* inputN;
        Connection* output0;
        Connection* output1;
        
        void process() override;

        void setup() override;
        Voice voices[NUM_VOICES];
};
