#include "MixerTrack.h"
#include "Project.h"

MixerTrack::MixerTrack(Project* project) {


}


MixerTrack::~MixerTrack() {

}

void MixerTrack::process(float* outputBuffer, int bufferSize) {

    float childBuffer[bufferSize] = {0.0f}; // stack buffer initialized to 0


    for (size_t i = 0; i < childTracks.size(); i++) {
        // Process each child track and store its result in the childBuffer
        childTracks[i]->process(childBuffer, bufferSize);
    }
    for (size_t i = 0; i < childInstruments.size(); i++) {
        childInstruments[i]->process(childBuffer, bufferSize);
    }
    rack.process(childBuffer, outputBuffer, bufferSize);

}
