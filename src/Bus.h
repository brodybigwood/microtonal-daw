#pragma once

#include <cstdint>
#include <memory>
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

class Note;

struct ActiveNote {
    std::shared_ptr<Note> note;
    int voiceId = 0;
    int positionId = 0;
    float sourceStartBeat = 0.f;
    float sourceEndBeat = 0.f;
    float sourcePitch = 0.f;
    int sourceChannel = 0;
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

    // Latency compensation: when pdcDelay > 0 the consumer reads a delayed copy of the
    // source block instead of the source buffer, so parallel paths stay aligned.
    std::vector<std::vector<float>> pdcLine;
    std::vector<float> pdcBuffer;
    int pdcDelay = 0;
    int pdcWrite = 0;
    // Cumulative latency at this connection (source chain for an output, aligned arrival for
    // an input). Shown on hover.
    int latency = 0;
    // For input connections: how many channels the connected source actually carries.
    int sourceChannels = 1;

    void setPdcDelay(int samples) {
        if (samples <= 0) {
            pdcDelay = 0;
            pdcWrite = 0;
            pdcLine.clear();
            pdcBuffer.clear();
            return;
        }
        if (pdcDelay != samples) {
            pdcDelay = samples;
            pdcWrite = 0;
            pdcLine.assign(static_cast<size_t>(numChannels), std::vector<float>(static_cast<size_t>(samples), 0.0f));
        }
    }

    float* channel(int ch) {
        const size_t off = static_cast<size_t>(ch) * static_cast<size_t>(bufferSize);
        if (pdcDelay > 0 && pdcBuffer.size() >= off + static_cast<size_t>(bufferSize))
            return pdcBuffer.data() + off;
        return buffer + ch * bufferSize;
    }

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
