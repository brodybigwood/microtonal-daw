#pragma once

#include "Bus.h"
#include "Note.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

enum TrackType{
    Audio = 0,
    Automation = 1,
    Notes = 2
};

struct Connection;

class Track {
    public:
        
        std::vector<std::shared_ptr<Note>> dispatched;

        Connection* connection;

        TrackType type = TrackType::Notes;
        TrackType& getType();

        void setType();

        uint16_t id;

        void process(float* input, int bufferSize);

        float** buffer = nullptr;
        int bufferSize;

        std::vector<Event>** events;
        void addEvent(Event);

        void fromJSON(json);
        json toJSON();

};
