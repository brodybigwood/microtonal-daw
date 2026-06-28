#pragma once

#include "Node.h"
#include <cstring>

// Biquad filter (direct form I): y = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
struct Biquad {
    float b0=0, b1=0, b2=0, a1=0, a2=0;
    float x1=0, x2=0, y1=0, y2=0;
    float process(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
    void reset() { x1=x2=y1=y2=0; }

    // Low-shelf: boost/cut below fc
    static Biquad lowShelf(float fc, float fs, float gainDB) {
        Biquad f;
        float A = std::pow(10.0f, gainDB/40.0f);
        float w0 = 2.0f*M_PI*fc/fs;
        float cosW = std::cos(w0);
        float sinW = std::sin(w0);
        float alpha = sinW/2.0f * std::sqrt((A+1.0f/A)*(1.0f/0.707f - 1.0f) + 2.0f);
        float a0 = (A+1.0f) + (A-1.0f)*cosW + 2.0f*std::sqrt(A)*alpha;
        f.b0 = A*((A+1.0f) - (A-1.0f)*cosW + 2.0f*std::sqrt(A)*alpha) / a0;
        f.b1 = 2.0f*A*((A-1.0f) - (A+1.0f)*cosW) / a0;
        f.b2 = A*((A+1.0f) - (A-1.0f)*cosW - 2.0f*std::sqrt(A)*alpha) / a0;
        f.a1 = -2.0f*((A-1.0f) + (A+1.0f)*cosW) / a0;
        f.a2 = ((A+1.0f) + (A-1.0f)*cosW - 2.0f*std::sqrt(A)*alpha) / a0;
        return f;
    }

    // Peaking EQ
    static Biquad peak(float fc, float fs, float gainDB, float Q) {
        Biquad f;
        float A = std::pow(10.0f, gainDB/40.0f);
        float w0 = 2.0f*M_PI*fc/fs;
        float cosW = std::cos(w0);
        float sinW = std::sin(w0);
        float alpha = sinW/(2.0f*Q);
        float a0 = 1.0f + alpha/A;
        f.b0 = (1.0f + alpha*A) / a0;
        f.b1 = (-2.0f*cosW) / a0;
        f.b2 = (1.0f - alpha*A) / a0;
        f.a1 = (-2.0f*cosW) / a0;
        f.a2 = (1.0f - alpha/A) / a0;
        return f;
    }
};

struct EarChain {
    Biquad shadow;   // head shadow shelf
    Biquad pinna1;   // concha boost  ~4.5kHz
    Biquad pinna2;   // pinna notch   ~7-11kHz
    float shouldTap = 0;  // shoulder reflection level
};

struct SurroundChannel {
    float* delayL = nullptr;
    float* delayR = nullptr;
    int delayLen = 0;
    int writePos = 0;
    int delaySamplesL = 0;
    int delaySamplesR = 0;
    float gainL = 1.0f;
    float gainR = 1.0f;
    EarChain earL;
    EarChain earR;
    int shoulderDelayL = 0;
    int shoulderDelayR = 0;

    void alloc(int maxDelay) {
        delayLen = maxDelay + 1;
        delayL = new float[static_cast<size_t>(delayLen)]{};
        delayR = new float[static_cast<size_t>(delayLen)]{};
    }
    ~SurroundChannel() { delete[] delayL; delete[] delayR; }
};

class SurroundToStereoNode : public Node {
public:
    SurroundToStereoNode(uint16_t, NodeManager*);

    void process() override;
    void setup() override;
    void renderContent(SDL_Renderer*) override;

    Connection* in[6]{};
    Connection* out = nullptr;
    SurroundChannel ch[6];
    int maxDelaySamples = 0;

    static constexpr float angles[6] = {-30, 30, -90, 90, -150, 150};
    static constexpr float ITD_MAX_MS = 0.65f;

    Knob mix = Knob(1.0f, TEX_W*0.5f - 40, TEX_H*0.5f, 80, "assets/knobs/1.png",
                    -135, 135, "Width");
    Knob room = Knob(0.3f, TEX_W*0.5f + 80, TEX_H*0.5f, 80, "assets/knobs/1.png",
                     -135, 135, "Room");
};
