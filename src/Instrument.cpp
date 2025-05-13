#include "Instrument.h"
#include "Project.h"

Instrument::Instrument(Project* project) {

    outputs.push_back(&project->tracks[0]);

    auto filepath = "/usr/lib/vst3/Vital.vst3/Contents/x86_64-linux/Vital.so";
 

}
Instrument::~Instrument() {

}


