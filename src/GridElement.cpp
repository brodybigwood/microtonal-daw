#include "GridElement.h"
#include "fract.h"
#include "Project.h"

GridElement::GridElement() {
}

void GridElement::createPos(fract startTime, uint16_t trackID) {
    uint16_t id = GridElement::id_pool()->newID();

    Position pos{
        fract{},
        startTime,
        fract{16,1} + startTime,
        fract{16,1},
        trackID,
        id
    };
    positions.push_back(pos);
}

idManager* GridElement::id_pool() {
    static idManager im;
    return &im;
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
        p["trackID"] = pos.trackID;
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
            p["trackID"],
            p["id"],
        };
        GridElement::id_pool()->reserveID(p["id"]);
        positions.push_back(pos);
    }
}
