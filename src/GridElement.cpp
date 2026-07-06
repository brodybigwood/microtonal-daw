#include "GridElement.h"
#include "Project.h"
#include "Note.h"
#include "PianoRollInternal.h"
#include <algorithm>

GridElement::GridElement(Project* p, ArrangerNode* n) : project(p), parentNode(n) {
}

void GridElement::createPos(std::vector<std::pair<int, int>> startPairs, std::vector<std::pair<int, int>> endPairs, uint16_t trackID,
                            int rhythmEdoSteps, std::vector<std::pair<int, int>> rhythmEdoLower, std::vector<std::pair<int, int>> rhythmEdoUpper) {
    uint16_t pid = pos_id_pool->newID();

    Position* pos = new Position{
        startPairs,
        endPairs,
        std::vector<std::pair<int, int>>{},
        trackID,
        static_cast<int>(pid),
        this,
        rhythmEdoSteps,
        std::move(rhythmEdoLower),
        std::move(rhythmEdoUpper)
    };
    positions.push_back(pos);
}

GridElement::~GridElement() {
    SDL_DestroyTexture(texture);
    texture = nullptr;
    for (auto* p : positions) {
        pos_id_pool->releaseID(static_cast<uint16_t>(p->id));
        delete p;
    }
    positions.clear();
}

static json pairsToJson(const std::vector<std::pair<int, int>>& v) {
    json j = json::array();
    for (const auto& pr : v)
        j.push_back(json::array({pr.first, pr.second}));
    return j;
}

static std::vector<std::pair<int, int>> pairsFromJson(const json& j) {
    std::vector<std::pair<int, int>> out;
    if (j.is_array()) {
        for (const auto& el : j)
            if (el.is_array() && el.size() >= 2)
                out.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    return out;
}

json GridElement::toJSON() {
    json j = json::array();
    for (auto position : positions)
        j.push_back(positionToJson(*position));
    return j;
}

void GridElement::fromJSON(json j) {
    for (json& p : j) {
        auto* pos = new Position{
            pairsFromJson(p.value("startOffsetPairs", json::array())),
            pairsFromJson(p.value("rhythmIntegerPairs", json::array())),
            pairsFromJson(p.value("rhythmEndIntegerPairs", json::array())),
            static_cast<uint16_t>(p.value("trackID", 0)),
            p.value("id", 0),
            this,
            p.value("rhythmEdoSubdivisionSteps", 1),
            pairsFromJson(p.value("rhythmEdoLowerVector", json::array())),
            pairsFromJson(p.value("rhythmEdoUpperVector", json::array()))
        };
        pos_id_pool->reserveID(static_cast<uint16_t>(p["id"].get<int>()));
        positions.push_back(pos);
    }
}

json GridElement::positionToJson(const Position& pos) {
    json p;
    p["startOffsetPairs"] = pairsToJson(pos.startOffsetPairs);
    p["rhythmIntegerPairs"] = pairsToJson(pos.rhythmIntegerPairs);
    p["rhythmEndIntegerPairs"] = pairsToJson(pos.rhythmEndIntegerPairs);
    p["trackID"] = pos.trackID;
    p["id"] = pos.id;
    p["rhythmEdoSubdivisionSteps"] = pos.rhythmEdoSubdivisionSteps;
    p["rhythmEdoLowerVector"] = pairsToJson(pos.rhythmEdoLowerVector);
    p["rhythmEdoUpperVector"] = pairsToJson(pos.rhythmEdoUpperVector);
    return p;
}

void GridElement::applyPositionFromJson(Position* pos, const json& j) {
    if (!pos) return;
    pos->startOffsetPairs = pairsFromJson(j.at("startOffsetPairs"));
    pos->rhythmIntegerPairs = pairsFromJson(j.at("rhythmIntegerPairs"));
    pos->rhythmEndIntegerPairs = pairsFromJson(j.at("rhythmEndIntegerPairs"));
    pos->trackID = static_cast<uint16_t>(j.at("trackID").get<int>());
    pos->rhythmEdoSubdivisionSteps = j.value("rhythmEdoSubdivisionSteps", 1);
    pos->rhythmEdoLowerVector = pairsFromJson(j.value("rhythmEdoLowerVector", json::array()));
    pos->rhythmEdoUpperVector = pairsFromJson(j.value("rhythmEdoUpperVector", json::array()));
}

bool GridElement::removePositionById(int positionId, size_t* removedIndex) {
    for (size_t i = 0; i < positions.size(); ++i) {
        if (positions[i]->id == positionId) {
            if (removedIndex)
                *removedIndex = i;
            pos_id_pool->releaseID(static_cast<uint16_t>(positions[i]->id));
            delete positions[i];
            positions.erase(positions.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void GridElement::insertPositionAt(size_t index, const json& p) {
    auto* pos = new Position{
        pairsFromJson(p.at("startOffsetPairs")),
        pairsFromJson(p.at("rhythmIntegerPairs")),
        pairsFromJson(p.at("rhythmEndIntegerPairs")),
        static_cast<uint16_t>(p.at("trackID").get<int>()),
        p.at("id").get<int>(),
        this,
        p.value("rhythmEdoSubdivisionSteps", 1),
        pairsFromJson(p.value("rhythmEdoLowerVector", json::array())),
        pairsFromJson(p.value("rhythmEdoUpperVector", json::array()))
    };
    pos_id_pool->reserveID(static_cast<uint16_t>(p.at("id").get<int>()));
    index = std::min(index, positions.size());
    positions.insert(positions.begin() + static_cast<std::ptrdiff_t>(index), pos);
}
