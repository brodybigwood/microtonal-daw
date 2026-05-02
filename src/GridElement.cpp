#include "GridElement.h"
#include "fract.h"
#include "Project.h"
#include <algorithm>

GridElement::GridElement(Project* p, ArrangerNode* n) : project(p), parentNode(n) {
}

void GridElement::createPos(fract startTime, uint16_t trackID) {
    uint16_t id = GridElement::id_pool()->newID();

    Position* pos = new Position{
        fract{},
        startTime,
        fract{16,1} + startTime,
        fract{16,1},
        trackID,
        id,
        this
    };
    positions.push_back(pos);
}

idManager* GridElement::id_pool() {
    static idManager im;
    return &im;
}


GridElement::~GridElement() {
    SDL_DestroyTexture(texture);
    texture = nullptr;
    for (auto* p : positions) {
        id_pool()->releaseID(static_cast<uint16_t>(p->id));
        delete p;
    }
    positions.clear();
}

json GridElement::toJSON() {
    json j = json::array();
    for(auto position : positions) {
        auto& pos = *position;
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
    for(json& p : j) {
        Position* pos = new Position{
            fract::fromJSON(p["startOffset"]),
            fract::fromJSON(p["start"]),
            fract::fromJSON(p["end"]),
            fract::fromJSON(p["length"]),
            p["trackID"],
            p["id"],
            this
        };
        GridElement::id_pool()->reserveID(p["id"]);
        positions.push_back(pos);
    }
}

json GridElement::positionToJson(const Position& pos) {
    json p;
    p["startOffset"] = pos.startOffset.toJSON();
    p["start"] = pos.start.toJSON();
    p["end"] = pos.end.toJSON();
    p["length"] = pos.length.toJSON();
    p["trackID"] = pos.trackID;
    p["id"] = pos.id;
    return p;
}

void GridElement::applyPositionFromJson(Position* pos, const json& j) {
    if (!pos)
        return;
    pos->startOffset = fract::fromJSON(j.at("startOffset"));
    pos->start = fract::fromJSON(j.at("start"));
    pos->end = fract::fromJSON(j.at("end"));
    pos->length = fract::fromJSON(j.at("length"));
    pos->trackID = static_cast<uint16_t>(j.at("trackID").get<int>());
}

bool GridElement::removePositionById(int positionId, size_t* removedIndex) {
    for (size_t i = 0; i < positions.size(); ++i) {
        if (positions[i]->id == positionId) {
            if (removedIndex)
                *removedIndex = i;
            id_pool()->releaseID(static_cast<uint16_t>(positions[i]->id));
            delete positions[i];
            positions.erase(positions.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void GridElement::insertPositionAt(size_t index, const json& p) {
    Position* pos = new Position{
        fract::fromJSON(p.at("startOffset")),
        fract::fromJSON(p.at("start")),
        fract::fromJSON(p.at("end")),
        fract::fromJSON(p.at("length")),
        static_cast<uint16_t>(p.at("trackID").get<int>()),
        p.at("id").get<int>(),
        this
    };
    id_pool()->reserveID(p.at("id").get<int>());
    index = std::min(index, positions.size());
    positions.insert(positions.begin() + static_cast<std::ptrdiff_t>(index), pos);
}
