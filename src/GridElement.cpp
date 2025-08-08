#include "GridElement.h"
#include "EventManager.h"
#include "Plugin.h"
#include "fract.h"
#include "Instrument.h"
#include "Project.h"

GridElement::GridElement() {
}

void GridElement::createPos(fract startTime, Instrument* inst) {
    static int lastId = 0;
    int id = lastId++;
    Position pos{
        fract{},
        startTime,
        fract{16,1} + startTime,
        fract{16,1},
        inst,
        id
    };
    positions.push_back(pos);
}



GridElement::~GridElement() {

}

json GridElement::toJSON() {
    json j = json::array();
    for(auto& pos : positions) {
        json p;
        p["startOffset"] = pos.startOffset.toJSON();
        p["start"] = pos.start.toJSON();
        p["end"] = pos.end.toJSON();
        p["length"] = pos.length.toJSON();
        p["instrumentID"] = pos.instrument->id;
        p["id"] = pos.id;

        j.push_back(p);
    }
    return j;
}

void GridElement::fromJSON(json j) {
    auto project = Project::instance();
    for(json& p : j) {
        Instrument* instrument;

        int targetID = p["instrumentID"];
        auto it = std::find_if(project->instruments.begin(), project->instruments.end(),
                               [targetID](const Instrument* inst) { return inst->id == targetID; });
        if (it != project->instruments.end()) {
            instrument = *it;
        } else {
            continue; //nullptr / invalid id
        }


        Position pos{
            fract::fromJSON(p["startOffset"]),
            fract::fromJSON(p["start"]),
            fract::fromJSON(p["end"]),
            fract::fromJSON(p["length"]),
            instrument,
            p["id"],
        };
        positions.push_back(pos);
    }
}
