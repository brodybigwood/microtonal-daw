#include "Track.h"
#include "TrackManager.h"
#include <iostream>

void Track::addEvent(Event event) {
    if (!connection) return;
    (*events)->push_back(event);
}

void Track::process(float* input, int bufferSize) {
    if (!connection) return;
    switch(type) {
        case TrackType::Audio:
            std::memset(*buffer, 0, sizeof(float) * bufferSize);
            break;
        case TrackType::Automation:
            break;
        case TrackType::Notes:
            (*events)->clear();
            break;
    }
}

TrackType& Track::getType() {
    return type;
}

void Track::fromJSON(json j) {
    type = static_cast<TrackType>(j["type"].get<uint16_t>());
    id = j["id"].get<uint16_t>();
}

json Track::toJSON() {
    json j;

    j["type"] = type;
    j["id"] = id;

    return j;
}
