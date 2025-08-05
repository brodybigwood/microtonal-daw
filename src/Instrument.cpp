#include "Instrument.h"
#include "EventList.h"
#include "Project.h"
#include <cmath>

Instrument::Instrument(Project* project, int index) : index(index) {

    this->project = project;

    addDestination(0);
    this->id = (project->id_inst)++;
}
Instrument::~Instrument() {

}

void Instrument::addDestination(int trackIndex) {
    outputs.push_back(project->tracks[0]);

    project->tracks[0]->childInstruments.push_back(this);
}

void Instrument::process(float* outputBuffer, int bufferSize) {
    float tempBuffer[bufferSize] = {0.0f};
    EventList tempEventList = std::move(eventList);
    eventList.events.clear();
    rack.process(tempBuffer, outputBuffer, bufferSize, &tempEventList);

}

json Instrument::toJSON() {
    json j;
    return j;
}
