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
    j["name"] = name;
    j["id"] = id;
    j["rack"] = rack.toJSON();
    return j;
}

void Instrument::fromJSON(json js) {
    name = js["name"];
    id = js["id"];

    constexpr uint16_t NO_ID = std::numeric_limits<uint16_t>::max();

    if (js.contains("rack")) {
        json jr = js["rack"];
        Project::instance()->id_rack = jr.value("id", NO_ID);
        rack = Rack();
        rack.fromJSON(jr);
        Project::instance()->racks.push_back(&rack);
    }

    uint16_t max_id = 0;
    for (auto rack : Project::instance()->racks) {
        if (rack->id > max_id) max_id = rack->id;
    }
    Project::instance()->id_rack = max_id + 1;
}
