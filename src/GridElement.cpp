#include "GridElement.h"
#include "fract.h"
#include "Project.h"

GridElement::GridElement() {
}

void GridElement::createPos(fract startTime, int bus) {
    static int lastId = 0;
    int id = lastId++;
    Position pos{
        fract{},
        startTime,
        fract{16,1} + startTime,
        fract{16,1},
        bus,
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
        p["busID"] = pos.bus;
        p["id"] = pos.id;
        j.push_back(p);
    }
    return j;
}

void GridElement::fromJSON(json j) {
    auto project = Project::instance();
    for(json& p : j) {

        Position pos{
            fract::fromJSON(p["startOffset"]),
            fract::fromJSON(p["start"]),
            fract::fromJSON(p["end"]),
            fract::fromJSON(p["length"]),
            p["busID"],
            p["id"],
        };
        positions.push_back(pos);
    }
}
