#include "osc.h"
#include <cmath>
#include <iostream>
#include <algorithm>

OscillatorNode::OscillatorNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Oscillator) {
    output0 = new Connection;
    output0->type = DataType::Waveform;
    output0->dir = Direction::output;
    outputs.addConnection(output0);

    output1 = new Connection;
    output1->type = DataType::Waveform;
    output1->dir = Direction::output;
    outputs.addConnection(output1);

    inputN = new Connection;
    inputN->type = DataType::Events;
    inputN->dir = Direction::input;
    inputs.addConnection(inputN);

    params.push_back(&volume);
}

void OscillatorNode::process() {
// first process input

    if (!inputN->is_connected) { // just do zeroes
        if (output0->is_connected) {
            std::memset(output0->buffer, 0, bufferSize * sizeof(float));
        }

        if (output1->is_connected) {
            std::memset(output1->buffer, 0, bufferSize * sizeof(float));
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

    float* b0 = output0->buffer;
    float* b1 = output1->buffer;

    if (output0->is_connected) {
        std::memset(b0, 0, bufferSize * sizeof(float));
    }

    if (output1->is_connected) {
        std::memset(b1, 0, bufferSize * sizeof(float));
    }

    for (int i = 0; i < NUM_VOICES; i++) {
        auto& voice = voices[i];
        if (!voice.active) continue;
        voice.process(b0, b1, bufferSize, sampleRate, volume);
    }
}

void OscillatorNode::setup() {
}

void Voice::process(float* out0, float* out1, int& bufferSize, int& sampleRate, Parameter& volume) {
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

        if (out0) out0[i] += smp;
        if (out1) out1[i] += smp;

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
