#pragma once

#include <cstdint>
#include <vector>

enum noteEventType {
    noteOn = 1,
    noteOff = 0,
    pitchBend = 2
};

struct Event {
    noteEventType type;
    float num;
    int id;
    int sampleOffset;
};

struct Bus {
    int id;
};

struct EventBus : public Bus {
    std::vector<Event> events;
};

struct WaveformBus : public Bus {
    float* buffer;
    uint8_t bufferSize;
};

