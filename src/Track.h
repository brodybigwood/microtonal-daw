#pragma once

#include "Bus.h"
#include "Note.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

enum TrackType{
    Audio = 0,
    Notes = 1
};

struct Connection;

class Track {
    public:

        std::vector<ActiveNote> dispatched;

        Connection* connection;

        TrackType type = TrackType::Notes;
        TrackType& getType();

        void setType();

        uint16_t id;

        void process(float* input, int bufferSize);

        float** buffer = nullptr;
        int bufferSize;

        std::vector<Event>** events = nullptr;
        void addEvent(Event);

        void fromJSON(json);
        json toJSON();

};
