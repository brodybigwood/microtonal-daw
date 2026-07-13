#include "Note.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>

static void primesForSlots(int slotCount, std::vector<int>& out) {
    out.clear();
    if (slotCount <= 0)
        return;
    for (int p = 2; static_cast<int>(out.size()) < slotCount; ++p) {
        bool isPrime = true;
        for (int d = 2; d * d <= p; ++d) {
            if (p % d == 0) {
                isPrime = false;
                break;
            }
        }
        if (isPrime)
            out.push_back(p);
    }
}

float Note::midiFromPitchVector(const std::vector<std::pair<int, int>>& pairs) {
    double prod = 1.0;
    std::vector<int> primes;
    primesForSlots(static_cast<int>(pairs.size()), primes);
    for (size_t i = 0; i < pairs.size(); ++i) {
        const double e =
            static_cast<double>(pairs[i].first) / static_cast<double>(pairs[i].second);
        prod *= std::pow(static_cast<double>(primes[i]), e);
    }
    return 69.0f + static_cast<float>(12.0 * std::log2(prod));
}

float Note::beatsFromVector(const std::vector<std::pair<int, int>>& pairs) {
    double prod = 1.0;
    std::vector<int> primes;
    primesForSlots(static_cast<int>(pairs.size()), primes);
    for (size_t i = 0; i < pairs.size(); ++i) {
        const double e =
            static_cast<double>(pairs[i].first) / static_cast<double>(pairs[i].second);
        prod *= std::pow(static_cast<double>(primes[i]), e);
    }
    return static_cast<float>(std::log2(prod));
}

void Note::syncNumFromPitchVector() {
    num = midiFromPitchVector(pitchVector);
}

Note::Note(const std::vector<std::pair<int, int>>& startPairs, const std::vector<std::pair<int, int>>& endPairs) {
    rhythmVector = startPairs;
    rhythmEndVector = endPairs;
    pitchVector.clear();
    syncNumFromPitchVector();
}

Note::~Note() {
}

float Note::startBeats() const {
    return beatsFromVector(rhythmVector);
}

float Note::endBeats() const {
    return beatsFromVector(rhythmEndVector);
}

float Note::durationBeats() const {
    return endBeats() - startBeats();
}

json Note::toJSON() {
    json j;
    j["id"] = id;
    j["channel"] = channel;
    j["tuningMode"] = tuningMode;
    j["tuningAnchorHarmonic"] = tuningAnchorHarmonic;
    j["tuningEdoSubdivisionSteps"] = tuningEdoSubdivisionSteps;
    j["tuningEdoLowerVector"] = json::array();
    for (const auto& pr : tuningEdoLowerVector)
        j["tuningEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["tuningEdoUpperVector"] = json::array();
    for (const auto& pr : tuningEdoUpperVector)
        j["tuningEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    j["pitchVector"] = json::array();
    for (const auto& pr : pitchVector)
        j["pitchVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmVector"] = json::array();
    for (const auto& pr : rhythmVector)
        j["rhythmVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEndVector"] = json::array();
    for (const auto& pr : rhythmEndVector)
        j["rhythmEndVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoSubdivisionSteps"] = rhythmEdoSubdivisionSteps;
    j["rhythmEdoLowerVector"] = json::array();
    for (const auto& pr : rhythmEdoLowerVector)
        j["rhythmEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoUpperVector"] = json::array();
    for (const auto& pr : rhythmEdoUpperVector)
        j["rhythmEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    return j;
}

std::shared_ptr<Note> Note::fromJSON(json& input) {
    std::vector<std::pair<int, int>> startPairs;
    if (input.contains("rhythmVector") && input["rhythmVector"].is_array()) {
        for (const auto& el : input["rhythmVector"])
            if (el.is_array() && el.size() >= 2)
                startPairs.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    std::vector<std::pair<int, int>> endPairs;
    if (input.contains("rhythmEndVector") && input["rhythmEndVector"].is_array()) {
        for (const auto& el : input["rhythmEndVector"])
            if (el.is_array() && el.size() >= 2)
                endPairs.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    if (endPairs.empty()) {
        endPairs = startPairs;
    }
    auto nid = input.at("id").get<int>();
    auto channel = input.value("channel", 0);

    std::shared_ptr<Note> n = std::make_shared<Note>(startPairs, endPairs);
    n->id = nid;
    n->channel = channel;
    n->tuningMode = input.value("tuningMode", 0);
    n->tuningAnchorHarmonic = input.value("tuningAnchorHarmonic", 1);
    n->tuningEdoSubdivisionSteps = input.value("tuningEdoSubdivisionSteps", 12);
    if (input.contains("tuningEdoLowerVector") && input["tuningEdoLowerVector"].is_array()) {
        for (const auto& el : input["tuningEdoLowerVector"])
            if (el.is_array() && el.size() >= 2)
                n->tuningEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    if (input.contains("tuningEdoUpperVector") && input["tuningEdoUpperVector"].is_array()) {
        for (const auto& el : input["tuningEdoUpperVector"])
            if (el.is_array() && el.size() >= 2)
                n->tuningEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    if (input.contains("pitchVector") && input["pitchVector"].is_array()) {
        n->pitchVector.clear();
        for (const auto& el : input["pitchVector"]) {
            if (el.is_array() && el.size() >= 2)
                n->pitchVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    n->rhythmEdoSubdivisionSteps = input.value("rhythmEdoSubdivisionSteps", 1);
    if (input.contains("rhythmVector") && input["rhythmVector"].is_array()) {
        n->rhythmVector.clear();
        for (const auto& el : input["rhythmVector"])
            if (el.is_array() && el.size() >= 2)
                n->rhythmVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    if (input.contains("rhythmEdoLowerVector") && input["rhythmEdoLowerVector"].is_array()) {
        for (const auto& el : input["rhythmEdoLowerVector"])
            if (el.is_array() && el.size() >= 2)
                n->rhythmEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    if (input.contains("rhythmEdoUpperVector") && input["rhythmEdoUpperVector"].is_array()) {
        for (const auto& el : input["rhythmEdoUpperVector"])
            if (el.is_array() && el.size() >= 2)
                n->rhythmEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    n->syncNumFromPitchVector();
    return n;
}

void Note::applyUndoSnapshot(const json& j) {
    channel = j.value("channel", channel);
    tuningMode = j.value("tuningMode", 0);
    tuningAnchorHarmonic = j.value("tuningAnchorHarmonic", 1);
    tuningEdoSubdivisionSteps = j.value("tuningEdoSubdivisionSteps", 12);
    tuningEdoLowerVector.clear();
    if (j.contains("tuningEdoLowerVector") && j["tuningEdoLowerVector"].is_array()) {
        for (const auto& el : j["tuningEdoLowerVector"])
            if (el.is_array() && el.size() >= 2)
                tuningEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    tuningEdoUpperVector.clear();
    if (j.contains("tuningEdoUpperVector") && j["tuningEdoUpperVector"].is_array()) {
        for (const auto& el : j["tuningEdoUpperVector"])
            if (el.is_array() && el.size() >= 2)
                tuningEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    if (j.contains("pitchVector") && j["pitchVector"].is_array()) {
        pitchVector.clear();
        for (const auto& el : j["pitchVector"]) {
            if (el.is_array() && el.size() >= 2)
                pitchVector.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    rhythmEdoSubdivisionSteps = j.value("rhythmEdoSubdivisionSteps", 1);
    rhythmVector.clear();
    if (j.contains("rhythmVector") && j["rhythmVector"].is_array()) {
        for (const auto& el : j["rhythmVector"])
            if (el.is_array() && el.size() >= 2)
                rhythmVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    rhythmEndVector.clear();
    if (j.contains("rhythmEndVector") && j["rhythmEndVector"].is_array()) {
        for (const auto& el : j["rhythmEndVector"])
            if (el.is_array() && el.size() >= 2)
                rhythmEndVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    rhythmEdoLowerVector.clear();
    if (j.contains("rhythmEdoLowerVector") && j["rhythmEdoLowerVector"].is_array()) {
        for (const auto& el : j["rhythmEdoLowerVector"])
            if (el.is_array() && el.size() >= 2)
                rhythmEdoLowerVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    rhythmEdoUpperVector.clear();
    if (j.contains("rhythmEdoUpperVector") && j["rhythmEdoUpperVector"].is_array()) {
        for (const auto& el : j["rhythmEdoUpperVector"])
            if (el.is_array() && el.size() >= 2)
                rhythmEdoUpperVector.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    syncNumFromPitchVector();
}

json Note::tuningFieldsUndoToJSON() const {
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
    j["rhythmVector"] = json::array();
    for (const auto& pr : rhythmVector)
        j["rhythmVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoSubdivisionSteps"] = rhythmEdoSubdivisionSteps;
    j["rhythmEdoLowerVector"] = json::array();
    for (const auto& pr : rhythmEdoLowerVector)
        j["rhythmEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoUpperVector"] = json::array();
    for (const auto& pr : rhythmEdoUpperVector)
        j["rhythmEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    return j;
}

void Note::applyTuningFieldsUndoFromJSON(const json& j) {
    // Absent keys leave the field untouched: tuning snapshots carry only the
    // tuning vectors, and clearing e.g. rhythmVector here would silently
    // move the note start to 0 (empty pair vector = log2(1) seconds).
    auto applyPairVector = [&j](const char* key, std::vector<std::pair<int, int>>& out) {
        if (!j.contains(key) || !j[key].is_array()) return;
        out.clear();
        for (const auto& el : j[key])
            if (el.is_array() && el.size() >= 2)
                out.push_back({el[0].get<int>(), el[1].get<int>()});
    };
    tuningMode = j.value("tuningMode", tuningMode);
    tuningAnchorHarmonic = j.value("tuningAnchorHarmonic", tuningAnchorHarmonic);
    tuningEdoSubdivisionSteps = j.value("tuningEdoSubdivisionSteps", tuningEdoSubdivisionSteps);
    applyPairVector("tuningEdoLowerVector", tuningEdoLowerVector);
    applyPairVector("tuningEdoUpperVector", tuningEdoUpperVector);
    rhythmEdoSubdivisionSteps = j.value("rhythmEdoSubdivisionSteps", rhythmEdoSubdivisionSteps);
    applyPairVector("rhythmVector", rhythmVector);
    applyPairVector("rhythmEdoLowerVector", rhythmEdoLowerVector);
    applyPairVector("rhythmEdoUpperVector", rhythmEdoUpperVector);
    syncNumFromPitchVector();
}
