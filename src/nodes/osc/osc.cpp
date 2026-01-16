#include "osc.h"
#include <cmath>
#include <iostream>

OscillatorNode::OscillatorNode(uint16_t id) : Node(id) {
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
}

void OscillatorNode::process() {

// first process input

    if (!inputN->is_connected) return;

    auto eBus = getEvents(inputN->data);
    auto& events = eBus->events;

    for (auto& event : events) {
        switch (event.type) {
            case noteEventType::noteOn: {
                    //assign to a voice
                    for (auto voice : voices) {
                        if (!voice.active) {
                            voice.noteId = event.id;
                            voice.frequency = 440 * pow(2.0f,(event.num - 69) / 12.0f);
                            voice.wait = event.sampleOffset;
                            break;
                        }
                    }
                }
                break;
            case noteEventType::noteOff: {
                    //deactivate the corresponding voice
                    for (auto voice : voices) {
                        if (event.id == voice.noteId) {
                            voice.reset();
                            break;
                        }   
                    }
                }
                break;
            default:
                std::cerr << "unrecognized event type" << std::endl;
        }
    }

    voices[0].active = true;
    voices[1].active = true;

// now do output

    float* b0;
    float* b1;

    if (output0->is_connected) {
        auto bus = getWaveform(output0->data);
        b0 = bus->buffer;
    }

    if (output1->is_connected) {
        auto bus = getWaveform(output0->data);
        b1 = bus->buffer;
    }

    for (int i = 0; i < 64; i++) {
        auto& voice = voices[i];
        if (!voice.active) return;

        voice.process(b0, b1, bufferSize, sampleRate);
    }
}

void OscillatorNode::setup() {
}

void Voice::process(float* out0, float* out1, int& bufferSize, int& sampleRate) {
    for (int i = 0; i < bufferSize; i++) {
        if (wait > 0) {
            wait -=1;
            continue;
        }
        float smp = sin(phase);
        if (out0) out0[i] = smp;
        if (out1) out1[i] = smp;

        phase += 2 * M_PI * frequency / sampleRate;

        if (phase >= 2* M_PI) phase -= 2 * M_PI;
    } 
}

void Voice::reset() {
    active = false;
    phase = 0;
    noteId = -1;   
    wait = 0;
}
