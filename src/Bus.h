#pragma once

#include <cstdint>
#include <vector>
#include <string>
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
    int channel = 0;
};

enum DataType{
    Events = 0,
    Waveform = 1
};

enum Direction{
    input = 0,
    output = 1
};

enum class PortDisplayMode {
    RectLabels = 0,
    SquareIDs = 1
};

class NodeManager;

struct Connection{
    std::string label;
    int displayIndex = 0;
    float labelScale = 3.0f;
    DataType type;
    bool is_connected = false;
    uint16_t id;
    Direction dir;

    void render(SDL_Renderer*, bool);
    SDL_FRect srcRect();
    SDL_FRect rect;

    std::vector<Event>* events = nullptr;
    float* buffer = nullptr;
    int bufferSize = 0;
    int numChannels = 1;
    int minChannels = 1;  // what the owning node needs; numChannels = max(minChannels, downstream)
    int allocChannels = 1; // actual channels the buffer was allocated for

    float* channel(int ch) { return buffer + ch * bufferSize; }

    void allocateBuffer(int bs, int nch) {
        if (buffer) delete[] buffer;
        bufferSize = bs;
        numChannels = nch;
        buffer = bs > 0 ? new float[static_cast<size_t>(bs) * static_cast<size_t>(nch)]() : nullptr;
        allocChannels = nch;
    }
    void allocateBuffer(int bs) { allocateBuffer(bs, numChannels); }

    void updateNumChannels();

    int output_node = -1;
    int output_connection = -1;

    int input_node = -1;
    int input_connection = -1;
    NodeManager* nm = nullptr;
};
