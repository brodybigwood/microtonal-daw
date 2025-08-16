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
    outputs.clear();
}

void Instrument::addDestination(int trackIndex) {
    outputs.reserve(outputs.size() + 1);
    project->tracks[0]->childInstruments.reserve(project->tracks[0]->childInstruments.size() + 1);


    outputs.push_back(project->tracks[0]);
    project->tracks[0]->childInstruments.push_back(this);
}

void Instrument::process(audioData data) {
    EventList tempEventList = std::move(eventList);
    eventList.events.clear();
    generators.process(data, &tempEventList);

}

json Instrument::toJSON() {
    json j;
    j["name"] = name;
    j["id"] = id;
    j["generators"] = generators.toJSON();
    return j;
}

void Instrument::fromJSON(json js) {
    name = js["name"];
    id = js["id"];

    constexpr uint16_t NO_ID = std::numeric_limits<uint16_t>::max();

    if (js.contains("generators")) {
        json jr = js["generators"];
        Project::instance()->id_rack = jr.value("id", NO_ID);
        generators = Rack();
        generators.fromJSON(jr);
        Project::instance()->racks.push_back(&generators);
    }

    uint16_t max_id = 0;
    for (auto rack : Project::instance()->racks) {
        if (rack->id > max_id) max_id = rack->id;
    }
    Project::instance()->id_rack = max_id + 1;
}
