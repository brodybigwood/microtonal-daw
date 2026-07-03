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

float Note::midiFromPitchIntegerPairs(const std::vector<std::pair<int, int>>& pairs) {
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

float Note::secondsFromIntegerPairs(const std::vector<std::pair<int, int>>& pairs) {
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

void Note::syncNumFromPitchIntegerPairs() {
    num = midiFromPitchIntegerPairs(pitchIntegerPairs);
}

Note::Note(const std::vector<std::pair<int, int>>& rhythmPairs, float durSec, float /*legacy pitch*/) {
    rhythmIntegerPairs = rhythmPairs;
    durationSeconds = std::max(0.0f, durSec);
    pitchIntegerPairs.clear();
    syncNumFromPitchIntegerPairs();
}

Note::~Note() {
}

float Note::startSeconds() const {
    return secondsFromIntegerPairs(rhythmIntegerPairs);
}

float Note::endSeconds() const {
    return startSeconds() + durationSeconds;
}

json Note::toJSON() {
    json j;
    j["id"] = id;
    j["durationSeconds"] = durationSeconds;
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
    j["pitchIntegerPairs"] = json::array();
    for (const auto& pr : pitchIntegerPairs)
        j["pitchIntegerPairs"].push_back(json::array({pr.first, pr.second}));
    j["rhythmIntegerPairs"] = json::array();
    for (const auto& pr : rhythmIntegerPairs)
        j["rhythmIntegerPairs"].push_back(json::array({pr.first, pr.second}));
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
    std::vector<std::pair<int, int>> rhythmPairs;
    if (input.contains("rhythmIntegerPairs") && input["rhythmIntegerPairs"].is_array()) {
        for (const auto& el : input["rhythmIntegerPairs"])
            if (el.is_array() && el.size() >= 2)
                rhythmPairs.push_back({el[0].get<int>(), el[1].get<int>()});
    }
    auto nid = input.at("id").get<int>();
    auto channel = input.at("channel").get<int>();

    std::shared_ptr<Note> n = std::make_shared<Note>(rhythmPairs, input.value("durationSeconds", 1.0f), 0.f);
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
    if (input.contains("pitchIntegerPairs") && input["pitchIntegerPairs"].is_array()) {
        n->pitchIntegerPairs.clear();
        for (const auto& el : input["pitchIntegerPairs"]) {
            if (el.is_array() && el.size() >= 2)
                n->pitchIntegerPairs.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    n->rhythmEdoSubdivisionSteps = input.value("rhythmEdoSubdivisionSteps", 1);
    if (input.contains("rhythmIntegerPairs") && input["rhythmIntegerPairs"].is_array()) {
        n->rhythmIntegerPairs.clear();
        for (const auto& el : input["rhythmIntegerPairs"])
            if (el.is_array() && el.size() >= 2)
                n->rhythmIntegerPairs.push_back({el[0].get<int>(), el[1].get<int>()});
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
    n->syncNumFromPitchIntegerPairs();
    return n;
}

void Note::applyUndoSnapshot(const json& j) {
    durationSeconds = j.value("durationSeconds", 1.0f);
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
    if (j.contains("pitchIntegerPairs") && j["pitchIntegerPairs"].is_array()) {
        pitchIntegerPairs.clear();
        for (const auto& el : j["pitchIntegerPairs"]) {
            if (el.is_array() && el.size() >= 2)
                pitchIntegerPairs.push_back({el[0].get<int>(), el[1].get<int>()});
        }
    }
    rhythmEdoSubdivisionSteps = j.value("rhythmEdoSubdivisionSteps", 1);
    rhythmIntegerPairs.clear();
    if (j.contains("rhythmIntegerPairs") && j["rhythmIntegerPairs"].is_array()) {
        for (const auto& el : j["rhythmIntegerPairs"])
            if (el.is_array() && el.size() >= 2)
                rhythmIntegerPairs.push_back({el[0].get<int>(), el[1].get<int>()});
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
    syncNumFromPitchIntegerPairs();
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
    j["rhythmIntegerPairs"] = json::array();
    for (const auto& pr : rhythmIntegerPairs)
        j["rhythmIntegerPairs"].push_back(json::array({pr.first, pr.second}));
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
    tuningMode = j.value("tuningMode", tuningMode);
    tuningAnchorHarmonic = j.value("tuningAnchorHarmonic", tuningAnchorHarmonic);
    tuningEdoSubdivisionSteps = j.value("tuningEdoSubdivisionSteps", tuningEdoSubdivisionSteps);
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
    rhythmEdoSubdivisionSteps = j.value("rhythmEdoSubdivisionSteps", rhythmEdoSubdivisionSteps);
    rhythmIntegerPairs.clear();
    if (j.contains("rhythmIntegerPairs") && j["rhythmIntegerPairs"].is_array()) {
        for (const auto& el : j["rhythmIntegerPairs"])
            if (el.is_array() && el.size() >= 2)
                rhythmIntegerPairs.push_back({el[0].get<int>(), el[1].get<int>()});
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
    syncNumFromPitchIntegerPairs();
}
