#include "Region.h"
#include <iostream>
#include <climits>
#include <set>
#include <functional>
#include "Project.h"
#include "styles.h"
#include "ElementManager.h"

Region::Region(Project* p, ArrangerNode* n) : GridElement(p, n) {
    type = ElementType::region;
}

Region::~Region() {

}

void Region::draw(SDL_Renderer* renderer, float pixelsPerSecond, int h) {

    if(!texture) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 10000, 100);
    }
   
    auto target = SDL_GetRenderTarget(renderer); 
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

    for(std::shared_ptr<Note> note : notes) {
        float noteX = note->startBeats() * 100;
        float noteY = (128-note->num)/128.0f * 100;
        float noteEnd = note->endBeats() * 100;
        SDL_SetRenderDrawColor(renderer, colors.note[0],colors.note[1],colors.note[2],colors.note[3]);
        SDL_FRect noteRect = { noteX, noteY - 1, noteEnd - noteX, 2};
        SDL_RenderFillRect(renderer, &noteRect);
    }
    SDL_SetRenderTarget(renderer, target);

}

json Region::toJSON() {
    json j;
    j["name"] = name;
    j["releaseMS"] = releaseMS;
    j["id"] = id;
    j["type"] = ElementType::region;
    j["notes"] = json::array();
    for(auto e : notes) {
        j["notes"].push_back(e->toJSON());
    }
    j["positions"] = GridElement::toJSON();
    j["idManager"] = id_pool.toJSON();
    j["tuningMode"] = tuningMode;
    j["tuningAnchorHarmonic"] = tuningAnchorHarmonic;
    j["tuningHarmonicAnchorVector"] = json::array();
    for (const auto& pr : tuningHarmonicAnchorVector)
        j["tuningHarmonicAnchorVector"].push_back(json::array({pr.first, pr.second}));
    j["tuningEdoSubdivisionSteps"] = tuningEdoSubdivisionSteps;
    j["tuningEdoLowerVector"] = json::array();
    for (const auto& pr : tuningEdoLowerVector)
        j["tuningEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["tuningEdoUpperVector"] = json::array();
    for (const auto& pr : tuningEdoUpperVector)
        j["tuningEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoSubdivisionSteps"] = rhythmEdoSubdivisionSteps;
    j["rhythmEdoLowerVector"] = json::array();
    for (const auto& pr : rhythmEdoLowerVector)
        j["rhythmEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoUpperVector"] = json::array();
    for (const auto& pr : rhythmEdoUpperVector)
        j["rhythmEdoUpperVector"].push_back(json::array({pr.first, pr.second}));

    return j;
}

void Region::fromJSON(json j) {
    name = j["name"];
    releaseMS = j["releaseMS"];
    GridElement::fromJSON(j["positions"]);
    id = j["id"];
    type = j["type"];
    for (auto e : j["notes"]) {
        notes.push_back(Note::fromJSON(e));
        id_to_index[e["id"]] = notes.size() - 1;
    }
    id_pool.fromJSON(j["idManager"]);
    tuningMode = j.value("tuningMode", 0);
    tuningAnchorHarmonic = j.value("tuningAnchorHarmonic", 1);
    tuningHarmonicAnchorVector.clear();
    if (j.contains("tuningHarmonicAnchorVector") && j["tuningHarmonicAnchorVector"].is_array()) {
        for (const auto& el : j["tuningHarmonicAnchorVector"]) {
            if (el.is_array() && el.size() >= 2)
                tuningHarmonicAnchorVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    tuningEdoSubdivisionSteps = j.value("tuningEdoSubdivisionSteps", 12);
    tuningEdoLowerVector.clear();
    if (j.contains("tuningEdoLowerVector") && j["tuningEdoLowerVector"].is_array()) {
        for (const auto& el : j["tuningEdoLowerVector"]) {
            if (el.is_array() && el.size() >= 2)
                tuningEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    tuningEdoUpperVector.clear();
    if (j.contains("tuningEdoUpperVector") && j["tuningEdoUpperVector"].is_array()) {
        for (const auto& el : j["tuningEdoUpperVector"]) {
            if (el.is_array() && el.size() >= 2)
                tuningEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    rhythmEdoSubdivisionSteps = j.value("rhythmEdoSubdivisionSteps", 1);
    rhythmEdoLowerVector.clear();
    if (j.contains("rhythmEdoLowerVector") && j["rhythmEdoLowerVector"].is_array()) {
        for (const auto& el : j["rhythmEdoLowerVector"]) {
            if (el.is_array() && el.size() >= 2)
                rhythmEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    rhythmEdoUpperVector.clear();
    if (j.contains("rhythmEdoUpperVector") && j["rhythmEdoUpperVector"].is_array()) {
        for (const auto& el : j["rhythmEdoUpperVector"]) {
            if (el.is_array() && el.size() >= 2)
                rhythmEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
}

json Region::tuningUndoToJSON() const {
    json j;
    j["tuningMode"] = tuningMode;
    j["tuningAnchorHarmonic"] = tuningAnchorHarmonic;
    j["tuningEdoSubdivisionSteps"] = tuningEdoSubdivisionSteps;
    j["tuningEdoLowerVector"] = json::array();
    for (const auto& pr : tuningEdoLowerVector)
        j["tuningEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["tuningEdoUpperVector"] = json::array();
    for (const auto& pr : tuningEdoUpperVector)
        j["tuningEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    j["tuningHarmonicAnchorVector"] = json::array();
    for (const auto& pr : tuningHarmonicAnchorVector)
        j["tuningHarmonicAnchorVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoSubdivisionSteps"] = rhythmEdoSubdivisionSteps;
    j["rhythmEdoLowerVector"] = json::array();
    for (const auto& pr : rhythmEdoLowerVector)
        j["rhythmEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoUpperVector"] = json::array();
    for (const auto& pr : rhythmEdoUpperVector)
        j["rhythmEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    return j;
}

void Region::applyTuningUndoFromJSON(const json& j) {
    tuningMode = j.value("tuningMode", 0);
    tuningAnchorHarmonic = j.value("tuningAnchorHarmonic", 1);
    tuningEdoSubdivisionSteps = j.value("tuningEdoSubdivisionSteps", 12);
    tuningEdoLowerVector.clear();
    if (j.contains("tuningEdoLowerVector") && j["tuningEdoLowerVector"].is_array()) {
        for (const auto& el : j["tuningEdoLowerVector"]) {
            if (el.is_array() && el.size() >= 2)
                tuningEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    tuningEdoUpperVector.clear();
    if (j.contains("tuningEdoUpperVector") && j["tuningEdoUpperVector"].is_array()) {
        for (const auto& el : j["tuningEdoUpperVector"]) {
            if (el.is_array() && el.size() >= 2)
                tuningEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    tuningHarmonicAnchorVector.clear();
    if (j.contains("tuningHarmonicAnchorVector") && j["tuningHarmonicAnchorVector"].is_array()) {
        for (const auto& el : j["tuningHarmonicAnchorVector"]) {
            if (el.is_array() && el.size() >= 2)
                tuningHarmonicAnchorVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    rhythmEdoSubdivisionSteps = j.value("rhythmEdoSubdivisionSteps", 1);
    rhythmEdoLowerVector.clear();
    if (j.contains("rhythmEdoLowerVector") && j["rhythmEdoLowerVector"].is_array()) {
        for (const auto& el : j["rhythmEdoLowerVector"]) {
            if (el.is_array() && el.size() >= 2)
                rhythmEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    rhythmEdoUpperVector.clear();
    if (j.contains("rhythmEdoUpperVector") && j["rhythmEdoUpperVector"].is_array()) {
        for (const auto& el : j["rhythmEdoUpperVector"]) {
            if (el.is_array() && el.size() >= 2)
                rhythmEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
}

int Region::createNote(std::vector<std::pair<int, int>> startPairs, std::vector<std::pair<int, int>> endPairs, std::vector<std::pair<int, int>> pitchVector) {
    auto n = std::make_shared<Note>(startPairs, endPairs);
    n->pitchVector = std::move(pitchVector);
    n->syncNumFromPitchVector();
    notes.push_back(n);
    n->id = id_pool.newID();
    id_to_index[n->id] = notes.size() - 1;
    return n->id;
}

void Region::deleteNote(int id) {
    auto it = id_to_index.find(id);
    if (it == id_to_index.end()) return;
    const int idx = it->second;
    if (idx < 0 || static_cast<size_t>(idx) >= notes.size()) {
        id_to_index.erase(it);
        return;
    }
    
    notes.erase(notes.begin() + idx);
    id_to_index.erase(it);

    for (auto& [k, v] : id_to_index) {
        if (v > idx)
            --v;
    }

    id_pool.releaseID(id);
}

void Region::restoreNoteAt(std::shared_ptr<Note> n, size_t insertIndex) {
    if (!n)
        return;
    if (insertIndex > notes.size())
        insertIndex = notes.size();
    id_pool.reserveID(static_cast<uint16_t>(n->id));
    notes.insert(notes.begin() + insertIndex, std::move(n));
    id_to_index.clear();
    for (size_t i = 0; i < notes.size(); ++i)
        id_to_index[notes[i]->id] = static_cast<int>(i);
}
