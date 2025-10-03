#include "Note.h"
#include <SDL3/SDL.h>
#include "fract.h"
#include "ScaleManager.h"

TuningTable* Note::getScale() {
    if(scale == nullptr) {
        return ScaleManager::instance()->getLastScale();
    } else {
        return scale;
    }
}

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

std::shared_ptr<Note> Note::fromJSON(json& input) {
    auto num = input.at("num").get<float>();
    fract start;
    start.num = input.at("start").at("num").get<int>();
    start.den = input.at("start").at("den").get<int>();
    fract end;
    end.num = input.at("end").at("num").get<int>();
    end.den = input.at("end").at("den").get<int>();
    auto id = input.at("id").get<int>();
    auto channel = input.at("channel").get<int>();
    auto temperament = input.at("temperament").get<double>();

    std::shared_ptr<Note> n = std::make_shared<Note>(start,end,num,temperament);
    n->id = id;
    n->channel = channel;

    return n;
}

