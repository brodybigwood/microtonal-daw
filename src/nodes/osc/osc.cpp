#include "osc.h"
#include <cmath>
#include <iostream>
#include <algorithm>

OscillatorNode::OscillatorNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Oscillator) {
    output = new Connection;
    output->type = DataType::Waveform;
    output->dir = Direction::output;
    output->numChannels = 2;
    outputs.addConnection(output);

    inputN = new Connection;
    inputN->type = DataType::Events;
    inputN->dir = Direction::input;
    inputs.addConnection(inputN);

    params.push_back(&volume);
}

void OscillatorNode::process() {
// first process input

    if (!inputN->is_connected) { // just do zeroes
        if (output->is_connected) {
            std::memset(output->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(output->numChannels) * sizeof(float));
        }
        return;
    }

    for (auto& event : *(inputN->events)) {
        switch (event.type) {
            case noteEventType::noteOn: {
                    //assign to a voice
                    for (int i = 0; i < NUM_VOICES; ++i) {
                        auto& voice = voices[i];
                        if (!voice.active) {
                            voice.noteId = event.id;
                            voice.frequency = 440 * pow(2.0f,(event.num - 69) / 12.0f);
                            voice.wait_on = event.sampleOffset;
                            voice.active = true;
                            break;
                        }
                    }
                }
                break;
            case noteEventType::noteOff: {
                    //deactivate the corresponding voice
                    for (int i = 0; i < NUM_VOICES; ++i) {
                        auto& voice = voices[i];
                        if (event.id == voice.noteId && voice.active) {
                            voice.wait_off = event.sampleOffset;
                            break;
                        }
                    }
                }
                break;
            default:
                std::cerr << "unrecognized event type" << std::endl;
        }
    }

// now do output

    if (output->is_connected) {
        std::memset(output->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(output->numChannels) * sizeof(float));
    }

    for (int i = 0; i < NUM_VOICES; i++) {
        auto& voice = voices[i];
        if (!voice.active) continue;
        voice.process(output->buffer, output->numChannels, bufferSize, sampleRate, volume);
    }
}

void OscillatorNode::setup() {
}

void Voice::process(float* out, int numChannels, int& bufferSize, int& sampleRate, Parameter& volume) {
    for (int i = 0; i < bufferSize; i++) {
        if (wait_on > 0) {
            wait_on -=1;
            continue;
        }

        if (wait_off > 0) {
            wait_off -=1;
        }

        if (wait_off == 0) {
            reset();
            return;
        }

        float smp = sin(phase) * volume[i];

        for (int ch = 0; ch < numChannels; ++ch) {
            out[ch * bufferSize + i] += smp;
        }

        phase += 2 * M_PI * frequency / sampleRate;

        if (phase >= 2* M_PI) phase -= 2 * M_PI;
    }
}

void Voice::reset() {
    active = false;
    phase = 0;
    noteId = -1;   
    wait_on = 0;
    wait_off = -1;
}

Voice::Voice() {
}
