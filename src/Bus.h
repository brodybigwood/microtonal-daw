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

enum DataType{
    Events = 0,
    Waveform = 1
};

enum ConnectionType{
    bus = 0,
    node = 1
};

enum Direction{
    input = 0,
    output = 1
};

struct Connection{
    DataType type;
    bool is_connected = false;
    uint16_t id;
    Direction dir;

    void* data;
};

struct Bus {
    DataType type;
    Connection output = { 
        .dir = Direction::output
    };
    uint16_t id;
};

struct EventBus : public Bus {
    EventBus() { 
        type = DataType::Events; 
        output.type = DataType::Events;
    };
    std::vector<Event> events;
};

struct WaveformBus : public Bus {
    WaveformBus() { 
        type = DataType::Waveform;
        output.type = DataType::Waveform;
    };
    float* buffer;
    uint8_t bufferSize;
};

