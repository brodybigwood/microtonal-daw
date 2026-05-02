#include "Note.h"
#include <SDL3/SDL.h>

Note::Note(fract start, fract end, float num) {
    this->end = end;
    this->num = num;
    this->start = start;
}

Note::~Note() {
}

void Note::move(fract x, fract y) {
    (void)x;
    (void)y;
}

json Note::toJSON() {
    json j;
    j["num"] = num;
    j["start"] = { {"num", start.num}, {"den", start.den} };
    j["end"] = { {"num", end.num}, {"den", end.den} };
    j["id"] = id;
    j["channel"] = channel;
    j["harmonicNumber"] = harmonicNumber;
    j["tuningMode"] = tuningMode;
    j["tuningAnchorMidi"] = tuningAnchorMidi;
    j["tuningAnchorHarmonic"] = tuningAnchorHarmonic;
    j["tuningEdoAnchorMidi"] = tuningEdoAnchorMidi;
    j["tuningEdoStep"] = tuningEdoStep;
    return j;
}

std::shared_ptr<Note> Note::fromJSON(json& input) {
    auto nVal = input.at("num").get<float>();
    fract start;
    start.num = input.at("start").at("num").get<int>();
    start.den = input.at("start").at("den").get<int>();
    fract end;
    end.num = input.at("end").at("num").get<int>();
    end.den = input.at("end").at("den").get<int>();
    auto nid = input.at("id").get<int>();
    auto channel = input.at("channel").get<int>();

    std::shared_ptr<Note> n = std::make_shared<Note>(start, end, nVal);
    n->id = nid;
    n->channel = channel;
    n->harmonicNumber = input.value("harmonicNumber", 0);
    n->tuningMode = input.value("tuningMode", 0);
    n->tuningAnchorMidi = input.value("tuningAnchorMidi", 69.0f);
    n->tuningAnchorHarmonic = input.value("tuningAnchorHarmonic", 1);
    n->tuningEdoAnchorMidi = input.value("tuningEdoAnchorMidi", 69.0f);
    n->tuningEdoStep = input.value("tuningEdoStep", 1.0f);
    return n;
}
