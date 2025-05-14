#include "Instrument.h"
#include "Project.h"
#include <cmath>

Instrument::Instrument(Project* project) {

    this->project = project;

    addDestination(0);
    rack.addPlugin("/usr/lib/vst3/Vital.vst3/Contents/x86_64-linux/Vital.so");
    //rack.addPlugin("/home/brody/Downloads/Podolski_123_12092_Linux/Podolski-12092/Podolski/Podolski.64.so");
}
Instrument::~Instrument() {

}

void Instrument::addDestination(int trackIndex) {
    outputs.push_back(project->tracks[0]);

    project->tracks[0]->childInstruments.push_back(this);
}

void Instrument::process(float* outputBuffer, int bufferSize) {
    float tempBuffer[bufferSize] = {0.0f}; // stack buffer initialized to 0
    rack.process(tempBuffer, outputBuffer, bufferSize);


}