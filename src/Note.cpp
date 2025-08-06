#include "Note.h"
#include <SDL3/SDL.h>
#include "fract.h"


Note::Note(fract start, fract end, float num, double temperament) {
    this->end = end;
    this->num = num;
    this->temperament = temperament;
    this->start = start;
}

Note::~Note() {
    // Destructor logic, if any is needed in future
}

json Note::toJSON() {
    json j;
    j["num"] = num;
    j["start"] = { {"num", start.num}, {"den", start.den} };
    j["end"] = { {"num", end.num}, {"den", end.den} };
    j["id"] = id;
    j["channel"] = channel;
    j["temperament"] = temperament;

    return j;
}

void Note::fromJSON(json& input) {
    num = input.at("num").get<float>();
    start.num = input.at("start").at("num").get<int>();
    start.den = input.at("start").at("den").get<int>();
    end.num = input.at("end").at("num").get<int>();
    end.den = input.at("end").at("den").get<int>();
    id = input.at("id").get<int>();
    channel = input.at("channel").get<int>();
    temperament = input.at("temperament").get<double>();
}

