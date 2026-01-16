#pragma oce

#include "Node.h"

struct Voice {
    bool active = false;

    float phase = 0;
    void process(float frequency, float* out0, float* out1, int& bufferSize, int& sampleRate); //stereo out
};


class OscillatorNode : public Node {
    public:
        OscillatorNode(uint16_t);


        Connection* inputN;
        Connection* output0;
        Connection* output1;
        
        void process() override;

        void setup() override;
        Voice voices[64];
};
