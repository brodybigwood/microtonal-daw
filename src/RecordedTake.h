#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RecordedTakeNote {
    float pitch = 69.f;
    int channel = 0;
    double startBeat = 0.0;
    double endBeat = 0.0;
};

/** One continuous armed+recording interval on one track, handed from the DSP thread to the
 *  GUI thread when the take ends. */
struct RecordedTake {
    std::vector<int> managerPath;
    int nodeID = 0;
    uint16_t trackID = 0;
    bool isAudio = false;
    double startBeat = 0.0;
    double endBeat = 0.0;
    std::string filepath;  // audio takes: WAV written by the GUI thread before the action
    int sampleRate = 0;
    int channels = 0;
    size_t frames = 0;
    std::vector<float> audio;  // planar: channels * frames
    std::vector<RecordedTakeNote> notes;
};
