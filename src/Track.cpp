#include "Track.h"
#include "BusManager.h"
#include "TrackList.h"
#include <iostream>

void Track::setBus(uint8_t id) {
    BusManager* bm = BusManager::get();

    switch(type) {
        case TrackType::Automation:
            dstBus = bm->getBusW(id);
            break;
        case TrackType::Audio:
            dstBus = bm->getBusW(id);
            break;
        case TrackType::Notes:
            dstBus = bm->getBusE(id);
            events = bm->getBusE(id)->events;
            break;
        default:
            break;
    }

    if(dstBus != nullptr) busID = dstBus->id;
}

void Track::addEvent(Event event) {
    if (eventNum > MAX_EVENTS) return;
    if (events == nullptr) return;
    
    events[eventNum] = event;
    eventNum++;
}

void Track::process(float* input, int bufferSize) {
    switch(type) {
        case TrackType::Audio:
            break;
        case TrackType::Automation:
            break;
        case TrackType::Notes:
            break;
    }

    BusManager::get()->getBusE(busID)->numEvents = eventNum;
    eventNum = 0;
}

TrackType& Track::getType() {
    return type;
}

void Track::fromJSON(json j) {
    type = static_cast<TrackType>(j["type"].get<uint16_t>());
    id = j["id"].get<uint16_t>();
    int bid = j["busID"].get<int>();
    setBus(bid);
    inputChannel = j["inputChannel"].get<uint8_t>();
}

json Track::toJSON() {
    json j;

    j["type"] = type;
    j["id"] = id;
    j["busID"] = busID;
    j["inputChannel"] = inputChannel;

    return j;
}
