#include "osc.h"
#include <cmath>
#include <iostream>
#include "AudioManager.h"

OscillatorNode::OscillatorNode(uint16_t id) : Node(id, NodeType::Oscillator) {
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

    if (!inputN->is_connected) { // just do zeroes
        if (output0->is_connected) {
            auto bus = getWaveform(output0->data);
            std::memset(bus->buffer, 0, bufferSize * sizeof(float));
        }
    
        if (output1->is_connected) {
            auto bus = getWaveform(output1->data);
            std::memset(bus->buffer, 0, bufferSize * sizeof(float));
        }
        return;
    }

    auto input = getInput(inputN);
    EventBus* eBus = getEvents(input);
    Event* events = eBus->events;

    int& numEvents = eBus->numEvents; 

    for (size_t i = 0; i < numEvents; ++i) {
        auto& event = events[i];
        switch (event.type) {
            case noteEventType::noteOn: {
                    //assign to a voice
                    for (int i = 0; i < NUM_VOICES; ++i) {
                        auto& voice = voices[i];
                        if (!voice.active) {
                            voice.noteId = event.id;
                            voice.frequency = 440 * pow(2.0f,(event.num - 69) / 12.0f);

                            std::cout << "noteOn: " << event.num << std::endl;
                            voice.wait_on = event.sampleOffset;
                            std::cout << "offset: " << voice.wait_on << std::endl;

                            voice.active = true;
                            std::cout << "activated voice " << i << std::endl;
                            break;
                        }
                    }
                }
                break;
            case noteEventType::noteOff: {
                    //deactivate the corresponding voice
                    for (int i = 0; i < NUM_VOICES; ++i) {
                        auto& voice = voices[i];
                        if (event.id == voice.noteId) {
                            
                            std::cout << "noteOff: " << event.num << std::endl;
                            voice.wait_off = event.sampleOffset;
                            std::cout << "deactivated voice " << i << std::endl;
                            std::cout << "offset: " << voice.wait_off << std::endl;
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

    float* b0 = nullptr;
    float* b1 = nullptr;

    if (output0->is_connected) {
        auto bus = getWaveform(output0->data);
        b0 = bus->buffer;
        std::memset(b0, 0, bufferSize * sizeof(float));
    }

    if (output1->is_connected) {
        auto bus = getWaveform(output1->data);
        b1 = bus->buffer;
        std::memset(b1, 0, bufferSize * sizeof(float));
    }

    for (int i = 0; i < NUM_VOICES; i++) {
        auto& voice = voices[i];
        if (!voice.active) continue;
        voice.process(b0, b1, bufferSize, sampleRate);
    }
}

void OscillatorNode::setup() {
    for (int i = 0; i < NUM_VOICES; ++i) {
        auto& voice = voices[i];
        voice.update();
    }
}

void Voice::process(float* out0, float* out1, int& bufferSize, int& sampleRate) {
    for (int i = 0; i < bufferSize; i++) {
        if (wait_on > 0) {
            wait_on -=1;
            continue;
        }

        if (wait_off > 0) {
            wait_off -=1;
        } else if (wait_off == 0) {
            reset();
            return;
        }

        float smp = sin(phase) * adsr[samplesPassed++];

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
    samplesPassed = 0;
}

Voice::Voice() {
    update();
}

Voice::~Voice() {
    if (adsr) delete[] adsr;
}

void Voice::updateADSR() {
    const int& sampleRate = AudioManager::instance()->sampleRate;
    const int limit = MAX_ADSR * sampleRate;
    const int attackSamples = attack * sampleRate;
    const int decaySamples = decay * sampleRate;
    const int releaseSamples = release * sampleRate;

    const int decayEnd = attackSamples + decaySamples;
    const int releaseEnd = decayEnd + releaseSamples;
    for (int i = 0; i < limit; i++) {
        if (i < attackSamples) {
            adsr[i] = (float)i / attackSamples;
        } else if (i < decayEnd) {
            adsr[i] = 1.0f - (1.0f - sustain) * (float)(i - attackSamples) / decaySamples;
        } else if (i < releaseEnd) {
            adsr[i] = sustain - sustain * (float)(i - decayEnd) / releaseSamples;
        } else {
            adsr[i] = 0.0f;
        }
    }
}

void Voice::update() {
    if (adsr) delete[] adsr;
    adsr = new float[MAX_ADSR * AudioManager::instance()->sampleRate];
    updateADSR();
}
