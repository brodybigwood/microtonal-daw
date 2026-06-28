#pragma once

#include "Node.h"
#define NUM_VOICES 32

struct Voice {
    Voice();

    bool active = false;
    float frequency;
    void reset();
    int noteId = -1;
    float phase = 0;
    int wait_on = 0;
    int wait_off = -1;
    void process(float* out, int numChannels, int& bufferSize, int& sampleRate, Parameter& vol);
};


class OscillatorNode : public Node {
    public:
        OscillatorNode(uint16_t, NodeManager*);


        Connection* inputN;
        Connection* output;

        void process() override;

        void setup() override;
        Voice voices[NUM_VOICES];

        Knob volume = Knob(0.5, TEX_W / 2, TEX_H / 2, 200, "assets/knobs/1.png", -135, 135, "Volume");
};
