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

enum BusType{
    Events = 0,
    Waveform = 1
};

struct Bus {
    int id;
    BusType type;
};

struct EventBus : public Bus {
    EventBus() { type = BusType::Events; };
    std::vector<Event> events;
};

struct WaveformBus : public Bus {
    WaveformBus() { type = BusType::Waveform; };
    float* buffer;
    uint8_t bufferSize;
};

