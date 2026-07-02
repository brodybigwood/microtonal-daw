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

void Note::syncNumFromPitchIntegerPairs() {
    num = midiFromPitchIntegerPairs(pitchIntegerPairs);
}

Note::Note(fract start, fract end, float /*legacy pitch argument ignored*/) {
    this->start = start;
    this->end = end;
    pitchIntegerPairs.clear();
    syncNumFromPitchIntegerPairs();
}

Note::~Note() {
}

void Note::move(fract x, fract y) {
    (void)x;
    (void)y;
}

json Note::toJSON() {
    json j;
    j["start"] = { {"num", start.num}, {"den", start.den} };
    j["end"] = { {"num", end.num}, {"den", end.den} };
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
    j["pitchIntegerPairs"] = json::array();
    for (const auto& pr : pitchIntegerPairs)
        j["pitchIntegerPairs"].push_back(json::array({pr.first, pr.second}));
    return j;
}

std::shared_ptr<Note> Note::fromJSON(json& input) {
    fract start;
    start.num = input.at("start").at("num").get<int>();
    start.den = input.at("start").at("den").get<int>();
    fract end;
    end.num = input.at("end").at("num").get<int>();
    end.den = input.at("end").at("den").get<int>();
    auto nid = input.at("id").get<int>();
    auto channel = input.at("channel").get<int>();

    std::shared_ptr<Note> n = std::make_shared<Note>(start, end, 0.f);
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
    n->syncNumFromPitchIntegerPairs();
    return n;
}

void Note::applyUndoSnapshot(const json& j) {
    start.num = j.at("start").at("num").get<int>();
    start.den = j.at("start").at("den").get<int>();
    end.num = j.at("end").at("num").get<int>();
    end.den = j.at("end").at("den").get<int>();
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
    syncNumFromPitchIntegerPairs();
}
