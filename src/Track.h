#pragma once

#include "Bus.h"
#include "Note.h"
#include "RecordedTake.h"
#include <unordered_map>
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
        // Per-track input port on the arranger (Events for Notes tracks, Waveform for Audio).
        Connection* inputConnection = nullptr;

        TrackType type = TrackType::Notes;
        TrackType& getType();

        bool recordArmed = false;
        bool monitor = false;
        // Duplicate a mono track output onto both channels.
        bool stereoMono = false;
        // Width the track's content naturally produces (clips/input), before stereoize.
        int naturalChannels = 1;

        // DSP-only capture state for the take currently being recorded; never serialized.
        struct TakeCapture {
            bool active = false;
            double startBeat = 0.0;
            double endBeat = 0.0;
            int channels = 0;
            std::vector<float> audio;  // planar, channels * frames
            std::unordered_map<int, RecordedTakeNote> pendingNotes;  // keyed by event voice id
        };
        TakeCapture capture;

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
