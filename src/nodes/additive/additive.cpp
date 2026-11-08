#include "additive.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
}

AdditiveNode::AdditiveNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Additive) {
    output = new Connection;
    output->type = DataType::Waveform;
    output->dir = Direction::output;
    output->minChannels = 2;
    output->updateNumChannels();
    outputs.addConnection(output);

    inputN = new Connection;
    inputN->type = DataType::Events;
    inputN->dir = Direction::input;
    inputs.addConnection(inputN);

    inputT = new Connection;
    inputT->type = DataType::Events;
    inputT->dir = Direction::input;
    inputs.addConnection(inputT);

    unison.setValueRange(1.f, 4.f, 1.f);
    detune.setValueRange(0.f, 40.f, 0.5f);
    params.push_back(&volume);
    params.push_back(&pitchFromLength);
    params.push_back(&unison);
    params.push_back(&detune);

    for (int i = 0; i < kPartials; ++i) {
        Parameter& ratio = partialParams[static_cast<size_t>(i * 3 + 0)];
        Parameter& phase = partialParams[static_cast<size_t>(i * 3 + 1)];
        Parameter& amp = partialParams[static_cast<size_t>(i * 3 + 2)];
        ratio.value = ratio.defaultValue = static_cast<float>(i + 1); // harmonic series
        phase.value = phase.defaultValue = 0.f;
        amp.value = amp.defaultValue = (i == 0) ? 1.f : 0.f;          // one sine by default
        ratio.label = "Partial " + std::to_string(i + 1) + " Ratio";
        phase.label = "Partial " + std::to_string(i + 1) + " Phase";
        amp.label = "Partial " + std::to_string(i + 1) + " Amp";
        params.push_back(&ratio);
        params.push_back(&phase);
        params.push_back(&amp);
    }
}

void AdditiveNode::process() {
    if (!inputN->is_connected) {
        for (auto& v : voices) v.active = false;
        if (output->is_connected) {
            std::memset(output->buffer, 0,
                        static_cast<size_t>(bufferSize) * static_cast<size_t>(output->numChannels) * sizeof(float));
        }
        return;
    }

    const bool lengthMode = pitchFromLength.value > 0.5f;
    for (auto& event : *inputN->events) {
        if (event.type == noteEventType::noteOn) {
            // Re-use the voice already keyed to this note, else take a free one.
            Voice* target = nullptr;
            for (auto& v : voices)
                if (v.active && v.noteId == event.id) target = &v;
            if (!target)
                for (auto& v : voices)
                    if (!v.active) { target = &v; break; }
            if (!target) continue;
            target->noteId = event.id;
            target->baseFreq = lengthMode
                ? 440.f * std::pow(2.f, event.duration)            // pitch = 69 + 12 * beats
                : 440.f * std::pow(2.f, (event.num - 69.f) / 12.f); // full fractional pitch
            target->amp = std::clamp(event.velocity, 0.f, 1.f);
            target->waitOn = event.sampleOffset;
            target->waitOff = -1;
            // Unison: 1..4 copies spread symmetrically over +-detune/2 cents, each
            // starting on a different phase so the stack sounds analog, not locked.
            int n = static_cast<int>(std::lround(unison.value));
            n = std::clamp(n, 1, kMaxUnison);
            target->unisonCount = n;
            for (int u = 0; u < kMaxUnison; ++u) {
                const float offCents = (u < n && n > 1)
                    ? (static_cast<float>(u) / static_cast<float>(n - 1) - 0.5f) * detune.value
                    : 0.f;
                target->detuneMult[static_cast<size_t>(u)] =
                    std::pow(2.f, offCents / 1200.f);
                for (int p = 0; p < kPartials; ++p)
                    target->phase[static_cast<size_t>(u)][static_cast<size_t>(p)] =
                        std::fmod(0.618034 * u + 0.381966 * p, 1.0) * kTwoPi;
                for (int t = 0; t < kTimbreMax; ++t)
                    target->timbrePhase[static_cast<size_t>(u)][static_cast<size_t>(t)] = 0.0;
            }
            target->active = true;
        } else if (event.type == noteEventType::noteOff) {
            bool matched = false;
            for (auto& v : voices)
                if (v.active && v.noteId == event.id) {
                    v.waitOff = event.sampleOffset;
                    matched = true;
                    break;
                }
            if (!matched)
                SDL_Log("[additive] orphan note-off id=%d", event.id);
        }
    }

    // Timbre input: pitch relative to 69 picks a partial ratio, velocity its level.
    // These notes do not sound on their own; they change the spectrum of the melody.
    if (inputT->is_connected && inputT->events) {
        for (auto& event : *inputT->events) {
            if (event.type == noteEventType::noteOn) {
                TimbrePartial* slot = nullptr;
                for (auto& tp : timbrePartials)
                    if (tp.active && tp.noteId == event.id) slot = &tp;
                if (!slot)
                    for (auto& tp : timbrePartials)
                        if (!tp.active) { slot = &tp; break; }
                if (!slot) continue;
                slot->noteId = event.id;
                slot->ratio = std::pow(2.f, (event.num - 69.f) / 12.f);
                slot->amp = std::clamp(event.velocity, 0.f, 1.f);
                slot->active = true;
            } else if (event.type == noteEventType::noteOff) {
                for (auto& tp : timbrePartials)
                    if (tp.active && tp.noteId == event.id) { tp.active = false; break; }
            }
        }
    }

    const int chans = output->numChannels;
    if (output->is_connected) {
        std::memset(output->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(chans) * sizeof(float));
    }

    for (auto& v : voices) {
        if (!v.active) continue;
        for (int i = 0; i < bufferSize; ++i) {
            if (v.waitOn > 0) { --v.waitOn; continue; }
            if (v.waitOff >= 0) {
                if (v.waitOff == 0) { v.active = false; break; }
                --v.waitOff;
            }
            double sample = 0.0;
            const int nU = std::max(1, v.unisonCount);
            const double uniNorm = 1.0 / std::sqrt(static_cast<double>(nU));
            for (int p = 0; p < kPartials; ++p) {
                const Parameter& ratio = partialParams[static_cast<size_t>(p * 3 + 0)];
                const Parameter& phaseOff = partialParams[static_cast<size_t>(p * 3 + 1)];
                const Parameter& amp = partialParams[static_cast<size_t>(p * 3 + 2)];
                if (amp.value <= 0.f || ratio.value <= 0.f) continue;
                const double freq = static_cast<double>(v.baseFreq) * ratio.value;
                double sum = 0.0;
                for (int u = 0; u < nU; ++u) {
                    auto& ph = v.phase[static_cast<size_t>(u)][static_cast<size_t>(p)];
                    sum += std::sin(ph + kTwoPi * phaseOff.value);
                    ph += kTwoPi * freq * v.detuneMult[static_cast<size_t>(u)] / sampleRate;
                    if (ph >= kTwoPi) ph -= kTwoPi;
                }
                sample += sum * uniNorm * amp.value;
            }
            // Timbre partials from the second event input.
            for (int t = 0; t < kTimbreMax; ++t) {
                const TimbrePartial& tp = timbrePartials[static_cast<size_t>(t)];
                if (!tp.active || tp.amp <= 0.f) continue;
                const double freq = static_cast<double>(v.baseFreq) * tp.ratio;
                double sum = 0.0;
                for (int u = 0; u < nU; ++u) {
                    auto& ph = v.timbrePhase[static_cast<size_t>(u)][static_cast<size_t>(t)];
                    sum += std::sin(ph);
                    ph += kTwoPi * freq * v.detuneMult[static_cast<size_t>(u)] / sampleRate;
                    if (ph >= kTwoPi) ph -= kTwoPi;
                }
                sample += sum * uniNorm * tp.amp;
            }
            const float out = static_cast<float>(sample) * v.amp * volume.value;
            for (int ch = 0; ch < chans; ++ch)
                output->buffer[ch * bufferSize + i] += out;
        }
    }
}
