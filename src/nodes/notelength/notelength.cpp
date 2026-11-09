#include "notelength.h"
#include "Project.h"
#include <algorithm>

NoteLengthNode::NoteLengthNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::NoteLength) {
    in = new Connection;
    in->type = DataType::Events;
    in->dir = Direction::input;
    inputs.addConnection(in);

    out = new Connection;
    out->type = DataType::Events;
    out->dir = Direction::output;
    outputs.addConnection(out);

    length.setValueRange(0.01f, 8.0f, 0.05f);
    params.push_back(&length);
}

void NoteLengthNode::setup() {
    pending.clear();
    closedEarly.clear();
}

void NoteLengthNode::process() {
    if (!out || !out->events) return;
    out->events->clear();
    if (!in || !in->is_connected || !in->events) return;

    // Samples for one beat at the current playhead, from the tempo curve.
    float secPerBeat = 0.5f;
    if (project) {
        const float b0 = static_cast<float>(project->dspBeatPosition);
        const float a = project->dspTempoCurve.secondsForBeats(0.f, b0);
        const float b = project->dspTempoCurve.secondsForBeats(0.f, b0 + 1.f);
        if (b > a) secPerBeat = b - a;
    }
    const float sr = sampleRate > 0 ? static_cast<float>(sampleRate) : 48000.f;
    const int lenSamples = std::max(1, static_cast<int>(length.value * secPerBeat * sr));

    for (auto& ev : *in->events) {
        if (ev.type == noteOn) {
            out->events->push_back(ev);
            PendingNote p;
            // Absolute end of the clamped note measured from this block's start:
            // the note's own offset plus its clamped length. The expire pass
            // below then emits the off at that exact sample (and never before
            // the note-on's offset).
            p.remainingSamples = std::clamp(ev.sampleOffset, 0, bufferSize - 1) + lenSamples;
            p.pitch = static_cast<int>(std::lround(ev.num));
            p.channel = ev.channel;
            pending[ev.id] = p;
            closedEarly.erase(ev.id);
        } else if (ev.type == noteOff) {
            if (closedEarly.erase(ev.id) > 0) continue; // already closed early
            pending.erase(ev.id);
            out->events->push_back(ev);
        } else {
            out->events->push_back(ev);
        }
    }

    // Expire notes whose clamped length ends inside this block; the rest carry
    // the remaining distance into the next block.
    for (auto it = pending.begin(); it != pending.end();) {
        if (it->second.remainingSamples < bufferSize) {
            Event off{};
            off.type = noteOff;
            off.num = static_cast<float>(it->second.pitch);
            off.id = it->first;
            off.sampleOffset = std::clamp(it->second.remainingSamples, 0, bufferSize - 1);
            off.channel = it->second.channel;
            out->events->push_back(off);
            closedEarly.insert(it->first);
            it = pending.erase(it);
        } else {
            it->second.remainingSamples -= bufferSize;
            ++it;
        }
    }
}
