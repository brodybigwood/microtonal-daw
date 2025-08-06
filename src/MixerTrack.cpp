#include "MixerTrack.h"
#include "EventManager.h"
#include "Project.h"

MixerTrack::MixerTrack(Project* project) {

    this->id = (project->id_track)++;
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

json MixerTrack::toJSON() {
    json j;
    j["name"] = name;
    j["id"] = id;

    json arr1 = json::array();
    for (auto child : childInstruments) {
        arr1.push_back(child->id);
    }
    j["childInstruments"] = arr1;

    json arr2 = json::array();
    for (auto child : childTracks) {
        arr2.push_back(child->id);
    }
    j["childTracks"] = arr2;

    j["rack"] = rack.toJSON();

    return j;
}

void MixerTrack::fromJSON(json js) {

    constexpr uint16_t NO_ID = std::numeric_limits<uint16_t>::max();

    json j;
    if (js.contains("rack")) {
        j = js["rack"];
    }
    if (j.contains("rack")) {
        json jr = j["rack"];
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
