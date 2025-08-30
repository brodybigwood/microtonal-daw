#pragma once
#include <cstddef>

struct channel {
    float* buffer;
};

struct audioStream {
    channel* channels;
    size_t numChannels;
};

struct audioData {
    audioStream input;
    audioStream output;
    size_t bufferSize;
};

struct ProcessContext {
    int sampleRate;
    int bufferSize;
    int inputCount;
    int outputCount;
};
