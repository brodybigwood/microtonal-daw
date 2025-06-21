#include "Instrument.h"
#include "Project.h"
#include <cmath>

Instrument::Instrument(Project* project) {

    this->project = project;

    addDestination(0);
    rack.addPlugin("/usr/lib/vst3/Vital.vst3/Contents/x86_64-linux/Vital.so");

    rack.addPlugin("/home/brody/Downloads/surge-xt-linux-x86_64-1.3.4/lib/vst3/Surge XT.vst3/Contents/x86_64-linux/Surge XT.so");
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
