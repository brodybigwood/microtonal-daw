#include "Plugin.h"
#include <cmath>

Plugin::Plugin() {

}

Plugin::~Plugin() {

}
void Plugin::load() {

    
}

void Plugin::process(
    float* thrubuffer,
    int bufferSize
) {

    for (unsigned int i = 0; i < bufferSize; ++i) {
        float sample = 0.5f * sin(2.0 * M_PI * 440.0 * bufferSize/1000 * i);  // 440Hz sine wave
        thrubuffer[i] = sample;
    }
}