#include "envelope.h"
#include <algorithm>
#include <cmath>
#include <cstring>

EnvelopeNode::EnvelopeNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Envelope) {
    eventIn = new Connection;
    eventIn->type = DataType::Events;
    eventIn->dir = Direction::input;
    inputs.addConnection(eventIn);

    eventOut = new Connection;
    eventOut->type = DataType::Events;
    eventOut->dir = Direction::output;
    outputs.addConnection(eventOut);

    envelopeOut = new Connection;
    envelopeOut->type = DataType::Waveform;
    envelopeOut->dir = Direction::output;
    outputs.addConnection(envelopeOut);

    params.push_back(&attack);
    params.push_back(&decay);
    params.push_back(&sustain);
    params.push_back(&release);
}

float EnvelopeNode::mapTimeSeconds(float normalized) {
    // Exponential map: 1ms..10s
    const float lo = std::log(0.001f);
    const float hi = std::log(10.0f);
    return std::exp(lo + std::clamp(normalized, 0.0f, 1.0f) * (hi - lo));
}

void EnvelopeNode::setup() {
    // Keep existing voices through setup so live envelopes do not pop on update.
}

void EnvelopeNode::process() {
    if (!eventOut || !eventOut->events || !envelopeOut || !envelopeOut->buffer || bufferSize <= 0 || sampleRate <= 0) {
        return;
    }

    eventOut->events->clear();
    std::memset(envelopeOut->buffer, 0, static_cast<size_t>(bufferSize) * sizeof(float));

    const int attackSamples = std::max(1, static_cast<int>(mapTimeSeconds(attack.value) * sampleRate));
    const int decaySamples = std::max(1, static_cast<int>(mapTimeSeconds(decay.value) * sampleRate));
    const int releaseSamples = std::max(1, static_cast<int>(mapTimeSeconds(release.value) * sampleRate));
    const float sustainLevel = sustain.value;

    std::vector<std::vector<Event>> eventsAt(static_cast<size_t>(bufferSize));
    if (eventIn && eventIn->is_connected && eventIn->events) {
        for (const auto& ev : *(eventIn->events)) {
            const int at = std::clamp(ev.sampleOffset, 0, bufferSize - 1);
            eventsAt[static_cast<size_t>(at)].push_back(ev);
        }
    }

    for (int i = 0; i < bufferSize; ++i) {
        for (const auto& ev : eventsAt[static_cast<size_t>(i)]) {
            if (ev.type == noteEventType::noteOn) {
                VoiceState v;
                v.stage = Stage::Attack;
                v.level = 0.0f;
                v.releaseStep = 0.0f;
                voices[ev.id] = v;
                Event outEv = ev;
                outEv.sampleOffset = i;
                eventOut->events->push_back(outEv);
            } else if (ev.type == noteEventType::noteOff) {
                auto it = voices.find(ev.id);
                if (it == voices.end()) {
                    Event outEv = ev;
                    outEv.sampleOffset = i;
                    eventOut->events->push_back(outEv);
                    continue;
                }
                it->second.stage = Stage::Release;
                it->second.releaseStep = std::max(0.000001f, it->second.level / static_cast<float>(releaseSamples));
            } else {
                Event outEv = ev;
                outEv.sampleOffset = i;
                eventOut->events->push_back(outEv);
            }
        }

        float envLevel = 0.0f;
        std::vector<int> doneIds;
        doneIds.reserve(voices.size());

        for (auto& [noteId, voice] : voices) {
            switch (voice.stage) {
                case Stage::Attack:
                    voice.level += 1.0f / static_cast<float>(attackSamples);
                    if (voice.level >= 1.0f) {
                        voice.level = 1.0f;
                        voice.stage = Stage::Decay;
                    }
                    break;
                case Stage::Decay:
                    voice.level -= (1.0f - sustainLevel) / static_cast<float>(decaySamples);
                    if (voice.level <= sustainLevel) {
                        voice.level = sustainLevel;
                        voice.stage = Stage::Sustain;
                    }
                    break;
                case Stage::Sustain:
                    voice.level = sustainLevel;
                    break;
                case Stage::Release:
                    voice.level -= voice.releaseStep;
                    if (voice.level <= 0.0f) {
                        voice.level = 0.0f;
                        Event offEv;
                        offEv.type = noteEventType::noteOff;
                        offEv.id = noteId;
                        offEv.num = 0.0f;
                        offEv.sampleOffset = i;
                        eventOut->events->push_back(offEv);
                        doneIds.push_back(noteId);
                    }
                    break;
            }
            envLevel += voice.level;
        }

        for (int noteId : doneIds) {
            voices.erase(noteId);
        }

        envelopeOut->buffer[i] = std::clamp(envLevel, 0.0f, 1.0f);
    }
}

json EnvelopeNode::extraSerialize() {
    json j;
    j["attack"] = attack.value;
    j["decay"] = decay.value;
    j["sustain"] = sustain.value;
    j["release"] = release.value;
    return j;
}

void EnvelopeNode::extraDeSerialize(json j) {
    attack.value = j.value("attack", attack.value);
    decay.value = j.value("decay", decay.value);
    sustain.value = j.value("sustain", sustain.value);
    release.value = j.value("release", release.value);
}
