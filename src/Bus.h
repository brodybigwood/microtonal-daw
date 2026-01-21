#pragma once

#include <cstdint>
#include <vector>
#include <SDL3/SDL.h>

#define MAX_EVENTS 256

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

    SDL_FRect srcRect();
    void* data = nullptr;
};

struct Bus {
    DataType type;
    Connection output = { 
        .dir = Direction::output
    };
    uint16_t id;
    SDL_FRect dstRect;
};

struct EventBus : public Bus {
    EventBus() { 
        type = DataType::Events; 
        output.type = DataType::Events;
    };
    int numEvents = 0;
    Event events[MAX_EVENTS];
};

struct WaveformBus : public Bus {
    WaveformBus() { 
        type = DataType::Waveform;
        output.type = DataType::Waveform;
    };
    float* buffer = nullptr;
    int bufferSize;
};

