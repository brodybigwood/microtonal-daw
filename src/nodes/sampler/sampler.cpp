#include "sampler.h"
#include "NodeManager.h"
#include "NodeProcessor.h"
#include "UndoManager.h"
#include "ContextMenu.h"
#include "NodeEditor.h"
#include "styles.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <numeric>

#ifndef __EMSCRIPTEN__
#include <sndfile.h>
#endif

// ============================================================================
// SampleSlab
// ============================================================================

SampleSlab::SampleSlab()
    : volume(1.0f, 0.f, 0.f, 8.f, 40.f),
      pitch(69.0f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, ""),
      pan(0.5f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, ""),
      offset(0.f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, ""),
      cutoff(1.f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, ""),
      attack(0.f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, ""),
      decay(0.f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, ""),
      sustain(1.f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, ""),
      release(0.f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, "") {}

SampleSlab::~SampleSlab() {
    if (buffer) delete[] buffer;
    if (thumbnail) SDL_DestroyTexture(thumbnail);
}

void SampleSlab::loadFile(const std::string& path) {
#ifndef __EMSCRIPTEN__
    SF_INFO sfinfo{};
    SNDFILE* sndfile = sf_open(path.c_str(), SFM_READ, &sfinfo);
    if (!sndfile) return;

    if (buffer) { delete[] buffer; buffer = nullptr; }
    if (thumbnail) { SDL_DestroyTexture(thumbnail); thumbnail = nullptr; }

    sampleRate = sfinfo.samplerate;
    numChannels = sfinfo.channels;
    num_samples = static_cast<size_t>(sfinfo.frames);

    size_t interleavedSize = num_samples * static_cast<size_t>(numChannels);
    float* interleaved = new float[interleavedSize];
    size_t framesRead = static_cast<size_t>(sf_readf_float(sndfile, interleaved, sfinfo.frames));
    if (framesRead < num_samples) num_samples = framesRead;

    buffer = new float[num_samples * static_cast<size_t>(numChannels)];
    for (int ch = 0; ch < numChannels; ++ch)
        for (size_t i = 0; i < num_samples; ++i)
            buffer[static_cast<size_t>(ch) * num_samples + i] =
                interleaved[i * static_cast<size_t>(numChannels) + static_cast<size_t>(ch)];

    filepath = path;
    sf_close(sndfile);
    delete[] interleaved;

    // Reset cutoff/offset if they exceed new bounds
    cutoff.value = 1.f;
    offset.value = 0.f;
#else
    filepath = path;
#endif
}

// ============================================================================
// SamplerNode
// ============================================================================

SamplerNode::SamplerNode(uint16_t id, NodeManager* nm)
    : Node(id, nm, NodeType::Sampler) {
    name = "Sampler";

    vCount = 4;
    vx = new float[4]{0.f, NODE_W, NODE_W, 0.f};
    vy = new float[4]{0.f, 0.f, kNODE_H, kNODE_H};
    dstRect.h = kNODE_H;
    w = NODE_W;
    h = kNODE_H;

    masterEventIn = new Connection;
    masterEventIn->type = DataType::Events;
    masterEventIn->dir = Direction::input;
    masterEventIn->label = "Master";
    inputs.addConnection(masterEventIn);
}

SamplerNode::~SamplerNode() {
    closeSampleEditor();
    if (vx) delete[] vx;
    if (vy) delete[] vy;
    vx = nullptr; vy = nullptr;
}

void SamplerNode::setup() {}

void SamplerNode::rebuildConnections() {
    // Preserve existing connection wiring keyed by sample ID (not position).
    struct ConnWire { uint16_t srcNode, srcCon, dstNode, dstCon; bool connected = false; };
    std::unordered_map<int, ConnWire> inWiresById, outWiresById;

    // Helper: parse "S<id> Evt" or "S<id> Out" → <id>, or -1 on failure.
    auto parseSampleId = [](const std::string& label) -> int {
        if (label.size() < 2 || label[0] != 'S') return -1;
        size_t pos = 1;
        while (pos < label.size() && label[pos] >= '0' && label[pos] <= '9') ++pos;
        if (pos == 1) return -1;
        return std::stoi(label.substr(1, pos - 1));
    };

    for (size_t i = 1; i < inputs.connections.size(); ++i) {
        auto* c = inputs.connections[i];
        if (c == masterEventIn) continue;
        int sid = parseSampleId(c->label);
        if (sid < 0) continue;
        ConnWire w{};
        if (c->is_connected) {
            w.connected = true;
            w.srcNode = c->input_node;
            w.srcCon = c->input_connection;
            w.dstNode = c->output_node;
            w.dstCon = c->output_connection;
        }
        inWiresById[sid] = w;
    }
    for (auto* c : outputs.connections) {
        int sid = parseSampleId(c->label);
        if (sid < 0) continue;
        ConnWire w{};
        if (c->is_connected) {
            w.connected = true;
            w.srcNode = c->input_node;
            w.srcCon = c->input_connection;
            w.dstNode = c->output_node;
            w.dstCon = c->output_connection;
        }
        outWiresById[sid] = w;
    }

    // Remove per-sample inputs, keeping master and mapped-param connections.
    {
        std::vector<Connection*> keep;
        keep.push_back(masterEventIn);
        for (size_t i = 1; i < inputs.connections.size(); ++i) {
            auto* c = inputs.connections[i];
            if (c == masterEventIn) continue;
            if (c->type == DataType::Events) {
                // Per-sample event input — sever wiring and delete.
                if (c->is_connected)
                    nm->severConnectionNow(c->input_node, c->input_connection, id, c->id);
                inputs.id_pool.releaseID(c->id);
                inputs.ids.erase(c->id);
                delete c;
            } else {
                keep.push_back(c); // mapped connection or other — preserve
            }
        }
        inputs.connections = std::move(keep);
    }

    // Add per-sample event inputs.
    for (size_t i = 0; i < samples.size(); ++i) {
        auto* c = new Connection;
        c->type = DataType::Events;
        c->dir = Direction::input;
        c->label = "S" + std::to_string(samples[i]->id) + " Evt";
        inputs.addConnection(c);
    }

    // Remove all outputs.
    while (!outputs.connections.empty()) {
        auto* c = outputs.connections.back();
        if (c->is_connected)
            nm->severConnectionNow(id, c->id, c->output_node, c->output_connection);
        outputs.id_pool.releaseID(c->id);
        outputs.ids.erase(c->id);
        outputs.connections.pop_back();
        delete c;
    }

    // Add per-sample audio outputs.
    for (size_t i = 0; i < samples.size(); ++i) {
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::output;
        c->minChannels = samples[i]->stereoOut ? 2 : samples[i]->numChannels;
        c->updateNumChannels();
        c->label = "S" + std::to_string(samples[i]->id) + " Out";
        outputs.addConnection(c);
    }

    // Restore wiring keyed by sample ID.
    for (size_t i = 1; i < inputs.connections.size(); ++i) {
        auto* c = inputs.connections[i];
        if (c == masterEventIn) continue;
        int sid = parseSampleId(c->label);
        auto it = inWiresById.find(sid);
        if (it != inWiresById.end() && it->second.connected) {
            nm->makeNodeConnectionNow(it->second.srcNode, it->second.srcCon,
                                      c->output_node, c->output_connection);
        }
    }
    for (auto* c : outputs.connections) {
        int sid = parseSampleId(c->label);
        auto it = outWiresById.find(sid);
        if (it != outWiresById.end() && it->second.connected) {
            nm->makeNodeConnectionNow(id, c->id, it->second.dstNode, it->second.dstCon);
        }
    }

    // Rebuild ID maps.
    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);
    outputs.ids.clear();
    for (size_t i = 0; i < outputs.connections.size(); ++i)
        outputs.ids[outputs.connections[i]->id] = static_cast<uint16_t>(i);

    makeConnectionRects();
    if (nm) nm->markTopologyDirty();
}

// Short, light labels for the sample editor knobs (the full labels are for the API).
static void applySamplerKnobDisplay(SampleSlab& s) {
    s.pitch.displayLabel = "Pitch";
    s.pan.displayLabel = "Pan";
    s.offset.displayLabel = "Offset";
    s.cutoff.displayLabel = "Cut";
    s.attack.displayLabel = "Atk";
    s.decay.displayLabel = "Dec";
    s.sustain.displayLabel = "Sus";
    s.release.displayLabel = "Rel";
    const SDL_Color light{220, 220, 220, 255};
    s.pitch.labelColor = light;
    s.pan.labelColor = light;
    s.offset.labelColor = light;
    s.cutoff.labelColor = light;
    s.attack.labelColor = light;
    s.decay.labelColor = light;
    s.sustain.labelColor = light;
    s.release.labelColor = light;
}

void SamplerNode::rebuildSampleParams() {
    params.clear();
    params.reserve(samples.size() * 9);
    for (size_t i = 0; i < samples.size(); ++i) {
        SampleSlab& slab = *samples[i];
        slab.volume.bind(this, i * 9);
        params.push_back(&slab.volume);
        params.push_back(&slab.pitch);
        params.push_back(&slab.pan);
        params.push_back(&slab.offset);
        params.push_back(&slab.cutoff);
        params.push_back(&slab.attack);
        params.push_back(&slab.decay);
        params.push_back(&slab.sustain);
        params.push_back(&slab.release);
    }
}

void SamplerNode::addSample(const std::string& filepath) {
    auto slab = std::make_unique<SampleSlab>();
    if (!filepath.empty())
        slab->loadFile(filepath);

    samples.push_back(std::move(slab));
    size_t idx = samples.size() - 1;
    auto& s = *samples[idx];
    s.id = static_cast<int>(sampleIdPool_.newID());
    s.volume.value = 1.0f;
    s.pitch.value = 69.0f;
    s.pan.value = 0.5f;
    s.offset.value = 0.f;
    s.cutoff.value = 1.f;
    s.attack.value = 0.f;
    s.decay.value = 0.f;
    s.sustain.value = 1.f;
    s.release.value = 0.f;
    s.volume.label = "S" + std::to_string(s.id) + " Vol";
    s.pitch.label = "S" + std::to_string(s.id) + " Pitch";
    s.pan.label = "S" + std::to_string(s.id) + " Pan";
    s.offset.label = "S" + std::to_string(s.id) + " Offset";
    s.cutoff.label = "S" + std::to_string(s.id) + " Cutoff";
    s.attack.label = "S" + std::to_string(s.id) + " Atk";
    s.decay.label = "S" + std::to_string(s.id) + " Dec";
    s.sustain.label = "S" + std::to_string(s.id) + " Sus";
    s.release.label = "S" + std::to_string(s.id) + " Rel";
    applySamplerKnobDisplay(s);
    rebuildSampleParams();
    rebuildConnections();
}

void SamplerNode::insertSampleAt(size_t index, const std::string& filepath) {
    auto slab = std::make_unique<SampleSlab>();
    if (!filepath.empty())
        slab->loadFile(filepath);

    if (index >= samples.size()) {
        samples.push_back(std::move(slab));
        index = samples.size() - 1;
    } else {
        samples.insert(samples.begin() + static_cast<ptrdiff_t>(index), std::move(slab));
    }

    auto& s = *samples[index];
    s.id = static_cast<int>(sampleIdPool_.newID());
    s.volume.value = 1.0f;
    s.pitch.value = 69.0f;
    s.pan.value = 0.5f;
    s.offset.value = 0.f;
    s.cutoff.value = 1.f;
    s.attack.value = 0.f;
    s.decay.value = 0.f;
    s.sustain.value = 1.f;
    s.release.value = 0.f;
    s.volume.label = "S" + std::to_string(s.id) + " Vol";
    s.pitch.label = "S" + std::to_string(s.id) + " Pitch";
    s.pan.label = "S" + std::to_string(s.id) + " Pan";
    s.offset.label = "S" + std::to_string(s.id) + " Offset";
    s.cutoff.label = "S" + std::to_string(s.id) + " Cutoff";
    s.attack.label = "S" + std::to_string(s.id) + " Atk";
    s.decay.label = "S" + std::to_string(s.id) + " Dec";
    s.sustain.label = "S" + std::to_string(s.id) + " Sus";
    s.release.label = "S" + std::to_string(s.id) + " Rel";
    applySamplerKnobDisplay(s);

    rebuildSampleParams();

    rebuildConnections();
}

void SamplerNode::removeSample(size_t index) {
    if (index >= samples.size()) return;

    // Close or adjust editor window.
    if (editorWindow_) {
        size_t edIdx = editorWindow_->sampleIndex;
        if (edIdx == index) {
            closeSampleEditor();
        } else if (edIdx > index) {
            editorWindow_->setSample(edIdx - 1);
        }
    }

    // Stop voices referencing this sample.
    for (auto& v : voices) {
        if (v.sampleIndex == static_cast<int>(index))
            v.reset();
        else if (v.sampleIndex > static_cast<int>(index))
            v.sampleIndex--;
    }

    sampleIdPool_.releaseID(static_cast<uint16_t>(samples[index]->id));
    samples.erase(samples.begin() + static_cast<ptrdiff_t>(index));

    // Remove 9 params for this sample.
    size_t paramBase = index * 9;
    if (paramBase + 9 <= params.size())
        params.erase(params.begin() + static_cast<ptrdiff_t>(paramBase),
                     params.begin() + static_cast<ptrdiff_t>(paramBase + 9));

    rebuildConnections();
}

void SamplerNode::reorderSamples(const std::vector<size_t>& newOrder) {
    if (newOrder.size() != samples.size()) return;

    // Stop all active voices.
    for (auto& v : voices) v.reset();

    // Update editor window sample index if open.
    if (editorWindow_) {
        size_t oldIdx = editorWindow_->sampleIndex;
        for (size_t i = 0; i < newOrder.size(); ++i) {
            if (newOrder[i] == oldIdx) { editorWindow_->setSample(i); break; }
        }
    }

    // Reorder samples.
    std::vector<std::unique_ptr<SampleSlab>> reordered;
    reordered.reserve(samples.size());
    for (size_t idx : newOrder)
        reordered.push_back(std::move(samples[idx])); // moves pointers only
    samples = std::move(reordered);

    // Rebuild params from samples (old pointers were invalidated by the move).
    rebuildSampleParams();

    // Reorder per-sample connections (master stays at index 0).
    {
        std::vector<Connection*> reorderedIn;
        reorderedIn.push_back(inputs.connections[0]); // master
        for (size_t idx : newOrder)
            reorderedIn.push_back(inputs.connections[1 + idx]);
        inputs.connections = std::move(reorderedIn);
    }
    {
        std::vector<Connection*> reorderedOut;
        for (size_t idx : newOrder)
            reorderedOut.push_back(outputs.connections[idx]);
        outputs.connections = std::move(reorderedOut);
    }

    // Rebuild ID maps.
    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);
    outputs.ids.clear();
    for (size_t i = 0; i < outputs.connections.size(); ++i)
        outputs.ids[outputs.connections[i]->id] = static_cast<uint16_t>(i);

    makeConnectionRects();
    if (nm) nm->markTopologyDirty();
}

void SamplerNode::setSampleFile(size_t index, const std::string& filepath) {
    if (index >= samples.size()) return;
    for (auto& v : voices) {
        if (v.sampleIndex == static_cast<int>(index)) v.reset();
    }
    samples[index]->loadFile(filepath);
    if (index < outputs.connections.size()) {
        auto* c = outputs.connections[index];
        c->minChannels = samples[index]->stereoOut ? 2 : samples[index]->numChannels;
        c->updateNumChannels();
    }
    if (nm) nm->markTopologyDirty();
}

// ============================================================================
// process()
// ============================================================================

void SamplerNode::process() {
    // Zero-fill all sample outputs.
    for (size_t si = 0; si < samples.size(); ++si) {
        if (si >= outputs.connections.size()) break;
        auto* outCon = outputs.connections[si];
        if (!outCon->is_connected) continue;
        std::memset(outCon->buffer, 0,
            static_cast<size_t>(bufferSize) * static_cast<size_t>(outCon->numChannels) * sizeof(float));
    }

    float sr = static_cast<float>(sampleRate);

    // --- Process events FIRST (oscillator pattern: events -> voices) ---

    // Mode B: Master input (index 0).
    if (masterEventIn && masterEventIn->events) {
        for (auto& event : *(masterEventIn->events)) {
            if (event.type == noteEventType::noteOn && !samples.empty()) {
                int si = static_cast<int>(std::round(event.num - 69.0f));
                if (si < 0) si = 0;
                if (si >= static_cast<int>(samples.size()))
                    si = static_cast<int>(samples.size()) - 1;
                for (int vi = 0; vi < kMaxVoices; ++vi) {
                    if (!voices[vi].active) {
                        auto& nv = voices[vi];
                        nv.active = true;
                        nv.noteId = event.id;
                        nv.sampleIndex = si;
                        nv.phase = 0.f;
                        nv.envPhase = 0.f;
                        nv.pan = samples[si]->pan.value;
                        nv.wait_on = event.sampleOffset;
                        // Master is slot selection only: default pitch, then
                        // the slot's pitch-shift knob.
                        const float shift = samples[si]->pitch.value - 69.0f;
                        nv.pitchRatio = std::pow(2.f, shift / 12.f)
                            * (static_cast<float>(samples[si]->sampleRate) / sr);
                        break;
                    }
                }
            } else if (event.type == noteEventType::noteOff) {
                for (int vi = 0; vi < kMaxVoices; ++vi) {
                    if (voices[vi].active && voices[vi].noteId == event.id && voices[vi].wait_off < 0) {
                        voices[vi].wait_off = event.sampleOffset;
                        break;
                    }
                }
            }
        }
    }

    // Mode A: Per-sample inputs (indices 1..N).
    for (size_t si = 0; si < samples.size(); ++si) {
        size_t connIdx = 1 + si;
        if (connIdx >= inputs.connections.size()) break;
        auto* inCon = inputs.connections[connIdx];
        if (!inCon->events) continue;

        for (auto& event : *(inCon->events)) {
            if (event.type == noteEventType::noteOn) {
                for (int vi = 0; vi < kMaxVoices; ++vi) {
                    if (!voices[vi].active) {
                        auto& nv = voices[vi];
                        nv.active = true;
                        nv.noteId = event.id;
                        nv.sampleIndex = static_cast<int>(si);
                        nv.phase = 0.f;
                        nv.envPhase = 0.f;
                        nv.pan = samples[si]->pan.value;
                        nv.wait_on = event.sampleOffset;
                        // Slot input: the note's pitch, shifted by the slot's
                        // pitch-shift knob.
                        float semitones = (event.num - 69.0f)
                            + (samples[si]->pitch.value - 69.0f);
                        nv.pitchRatio = std::pow(2.f, semitones / 12.f)
                            * (static_cast<float>(samples[si]->sampleRate) / sr);
                        break;
                    }
                }
            } else if (event.type == noteEventType::noteOff) {
                for (int vi = 0; vi < kMaxVoices; ++vi) {
                    if (voices[vi].active && voices[vi].noteId == event.id
                        && voices[vi].sampleIndex == static_cast<int>(si) && voices[vi].wait_off < 0) {
                        voices[vi].wait_off = event.sampleOffset;
                        break;
                    }
                }
            }
        }
    }

    // --- Process active voices ---
    bool anySolo = false;
    for (auto& sp : samples) { if (sp->solo) { anySolo = true; break; } }

    for (int vi = 0; vi < kMaxVoices; ++vi) {
        auto& v = voices[vi];
        if (!v.active) continue;
        if (v.sampleIndex < 0 || v.sampleIndex >= static_cast<int>(samples.size())) continue;

        auto& slab = *samples[v.sampleIndex];

        bool shouldPlay = slab.buffer && !slab.mute && (!anySolo || slab.solo);
        auto* outCon = (v.sampleIndex < static_cast<int>(outputs.connections.size()))
            ? outputs.connections[v.sampleIndex] : nullptr;
        bool haveOutput = outCon && outCon->is_connected;

        int nch = haveOutput ? outCon->numChannels : 0;
        float* outBuf = haveOutput ? outCon->buffer : nullptr;
        float vol = slab.volume.value;
        float panL = std::cos(slab.pan.value * static_cast<float>(M_PI_2));
        float panR = std::sin(slab.pan.value * static_cast<float>(M_PI_2));

        float startPhase = slab.offset.value * static_cast<float>(slab.num_samples);
        float endPhase = slab.cutoff.value * static_cast<float>(slab.num_samples);
        float atkSec = slab.attack.value;
        float decSec = slab.decay.value;
        float susLev = slab.sustain.value;
        float relSec = slab.release.value;

        for (int i = 0; i < bufferSize; ++i) {
            if (v.wait_on > 0) { v.wait_on--; continue; }

            // Count down wait_off; when it reaches 0, enter release.
            if (v.wait_off > 0) {
                v.wait_off--;
                if (v.wait_off == 0) v.envPhase = 0.f; // start release
            }

            // ADSR envelope.
            float env = 0.f;
            float t = v.envPhase;
            if (v.wait_off < 0) {
                // Attack / Decay / Sustain (no note-off yet).
                if (t < atkSec && atkSec > 0.f)
                    env = t / atkSec;
                else if (t < atkSec + decSec && decSec > 0.f)
                    env = 1.f - (1.f - susLev) * ((t - atkSec) / decSec);
                else
                    env = susLev;
            } else {
                // Release (wait_off reached 0, now fading).
                if (relSec > 0.f)
                    env = susLev * (1.f - t / relSec);
                else
                    env = 0.f;
                if (env <= 0.f) { v.reset(); break; }
            }
            v.envLevel = env;
            v.envPhase += 1.f / sr;

            // Read sample — each channel from its own plane.
            float readPhase = startPhase + v.phase;
            if (readPhase >= endPhase || readPhase < startPhase) { v.reset(); break; }

            if (shouldPlay && haveOutput) {
                size_t idx = static_cast<size_t>(readPhase);
                float frac = readPhase - static_cast<float>(idx);
                int slabCh = slab.numChannels;

                for (int ch = 0; ch < nch; ++ch) {
                    int sch = (ch < slabCh) ? ch : 0;
                    float* chBuf = slab.buffer + static_cast<size_t>(sch) * slab.num_samples;
                    float chVal = 0.f;
                    if (idx + 1 < slab.num_samples)
                        chVal = chBuf[idx] + frac * (chBuf[idx + 1] - chBuf[idx]);
                    else if (idx < slab.num_samples)
                        chVal = chBuf[idx];

                    // Mono sources are duplicated across both channels (stereoized),
                    // and the pan control still places them in the stereo field.
                    float pg = 1.f;
                    if (nch >= 2)
                        pg = (ch == 0) ? panL : panR;
                    outBuf[static_cast<size_t>(ch) * static_cast<size_t>(bufferSize) + static_cast<size_t>(i)] +=
                        chVal * vol * env * pg;
                }
            }

            v.phase += v.pitchRatio;
        }
    }
}

// ============================================================================
// UI: renderContent
// ============================================================================

static float columnLeft(size_t i, float scroll) { return SamplerNode::kColPad + static_cast<float>(i) * (SamplerNode::kColW + SamplerNode::kColPad) - scroll; }

void SamplerNode::ensureThumbnail(size_t index, SDL_Renderer* R) {
    if (index >= samples.size()) return;
    auto& s = *samples[index];
    if (s.thumbnail || !s.buffer) return;

    int tw = static_cast<int>(kColW - 4);
    int th = static_cast<int>(kThumbH - 4);
    s.thumbnail = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, tw, th);
    if (!s.thumbnail) return;

    SDL_Texture* oldTarget = SDL_GetRenderTarget(R);
    SDL_SetRenderTarget(R, s.thumbnail);
    SDL_SetRenderDrawColor(R, 40, 40, 45, 255);
    SDL_RenderClear(R);

    float spp = static_cast<float>(s.num_samples) / static_cast<float>(tw);
    for (int px = 0; px < tw; ++px) {
        size_t s1 = static_cast<size_t>(static_cast<float>(px) * spp);
        size_t s2 = static_cast<size_t>(static_cast<float>(px + 1) * spp);
        if (s2 >= s.num_samples) break;
        float minVal = 0.f, maxVal = 0.f;
        for (size_t si = s1; si <= s2 && si < s.num_samples; ++si) {
            float v = s.buffer[si];
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
        }
        int top = th / 2 - static_cast<int>(maxVal * static_cast<float>(th / 2) * 0.9f);
        int bot = th / 2 - static_cast<int>(minVal * static_cast<float>(th / 2) * 0.9f);
        if (top < 0) top = 0;
        if (bot >= th) bot = th - 1;
        if (top <= bot) {
            // Use SDL_RenderLine for the vertical line.
            SDL_SetRenderDrawColor(R, 100, 180, 255, 255);
            SDL_RenderLine(R, static_cast<float>(px), static_cast<float>(top),
                              static_cast<float>(px), static_cast<float>(bot));
        }
    }
    SDL_SetRenderTarget(R, oldTarget);
}

void SamplerNode::openSampleEditor(size_t index) {
    closeSampleEditor();
    if (!project || !project->processor) return;
    auto* wm = project->processor->getWindowManager();
    if (!wm) return;

    auto win = std::make_unique<SampleEditorWindow>(this, index);
    SampleEditorWindow* ptr = win.get();
    ExpandedWindow* ew = wm->addWindow(std::move(win), 300, 400, "Sample Editor");
    if (!ew) return;
    ew->show();
    editorWindow_ = ptr;
}

void SamplerNode::closeSampleEditor() {
    if (editorWindow_) {
        editorWindow_->onRequestClose();
        editorWindow_ = nullptr;
    }
}

void SamplerNode::render(SDL_Renderer* R) {
    if (!R) return;
    if (!texture || !dstRect.h) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET, static_cast<int>(NODE_W), static_cast<int>(kNODE_H));
    }
    // Let the base class handle rendering pipeline.
    Node::render(R);
}

void SamplerNode::renderContent(SDL_Renderer* R) {
    SDL_SetRenderDrawColor(R, 35, 35, 40, 255);
    SDL_FRect bg{0, 0, NODE_W, kNODE_H};
    SDL_RenderFillRect(R, &bg);

    float listTop = kListTop + 24.f; // below port rects

    // Total content height of a column (without add button).
    float colContentH = kThumbH + kRowH + 3.f * kBtnSz + 6.f + kSliderH + 4.f;

    // Sample columns.
    for (size_t i = 0; i < samples.size(); ++i) {
        float cx = columnLeft(i, scrollOffset_);
        if (cx + kColW < 0.f || cx > NODE_W) continue;

        auto& slab = *samples[i];
        ensureThumbnail(i, R);

        // Column background.
        SDL_FRect colBg{cx, listTop, kColW, colContentH};
        SDL_SetRenderDrawColor(R, 50, 50, 55, 255);
        SDL_RenderFillRect(R, &colBg);
        if (hoveredSampleIndex_ == static_cast<int>(i))
            SDL_SetRenderDrawColor(R, 140, 180, 220, 255);
        else
            SDL_SetRenderDrawColor(R, 80, 80, 85, 255);
        SDL_RenderRect(R, &colBg);

        float cy = listTop + 2.f;

        // Thumbnail.
        SDL_FRect thumbR{cx + 2.f, cy, kColW - 4.f, kThumbH - 4.f};
        if (slab.thumbnail)
            SDL_RenderTexture(R, slab.thumbnail, nullptr, &thumbR);
        else {
            SDL_SetRenderDrawColor(R, 30, 30, 35, 255);
            SDL_RenderFillRect(R, &thumbR);
        }
        cy += kThumbH;

        // Index label at natural size, centered in column.
        std::string label = "S" + std::to_string(slab.id);
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, label.c_str(), label.size(), {200, 200, 200, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(R, surf);
            SDL_FRect lr{cx + (kColW - surf->w) / 2.f, cy, static_cast<float>(surf->w), static_cast<float>(surf->h)};
            SDL_RenderTexture(R, tex, nullptr, &lr);
            SDL_DestroySurface(surf);
            SDL_DestroyTexture(tex);
        }
        cy += kRowH;

        // Solo [S], Mute [M], stereo output [2/1].
        const float btnX = cx + (kColW - kBtnSz) * 0.5f;
        SDL_FRect soloR{btnX, cy, kBtnSz, kBtnSz};
        SDL_FRect muteR{btnX, cy + kBtnSz + 2.f, kBtnSz, kBtnSz};
        SDL_FRect stereoR{btnX, cy + 2.f * kBtnSz + 4.f, kBtnSz, kBtnSz};
        SDL_Color soloCol = slab.solo ? SDL_Color{255, 200, 50, 255} : SDL_Color{70, 70, 75, 255};
        SDL_Color muteCol = slab.mute ? SDL_Color{255, 60, 60, 255} : SDL_Color{70, 70, 75, 255};
        SDL_Color stereoCol = slab.stereoOut ? SDL_Color{80, 150, 220, 255} : SDL_Color{70, 70, 75, 255};
        SDL_SetRenderDrawColor(R, soloCol.r, soloCol.g, soloCol.b, soloCol.a);
        SDL_RenderFillRect(R, &soloR);
        SDL_SetRenderDrawColor(R, muteCol.r, muteCol.g, muteCol.b, muteCol.a);
        SDL_RenderFillRect(R, &muteR);
        SDL_SetRenderDrawColor(R, stereoCol.r, stereoCol.g, stereoCol.b, stereoCol.a);
        SDL_RenderFillRect(R, &stereoR);

        // Label the square buttons so they are recognisable.
        if (fonts.mainFont) {
            const char* lbls[3] = {"S", "M", slab.stereoOut ? "2" : "1"};
            SDL_FRect rects[3] = {soloR, muteR, stereoR};
            for (int bi = 0; bi < 3; ++bi) {
                SDL_Surface* bs = TTF_RenderText_Blended(fonts.mainFont, lbls[bi], 1, SDL_Color{235, 235, 235, 255});
                if (!bs) continue;
                SDL_Texture* bt = SDL_CreateTextureFromSurface(R, bs);
                if (bt) {
                    const float sc = std::min(1.0f, (rects[bi].w - 2.f) / static_cast<float>(bs->w));
                    SDL_FRect br{rects[bi].x + (rects[bi].w - bs->w * sc) * 0.5f,
                                 rects[bi].y + (rects[bi].h - bs->h * sc) * 0.5f,
                                 bs->w * sc, bs->h * sc};
                    SDL_RenderTexture(R, bt, nullptr, &br);
                    SDL_DestroyTexture(bt);
                }
                SDL_DestroySurface(bs);
            }
        }
        cy += 3.f * kBtnSz + 6.f;

        // Volume bar owns its own input and undo step.
        float sliderW = 10.f;
        float sliderX = cx + (kColW - sliderW) / 2.f;
        slab.volume.reposition(sliderX, cy, sliderW, kSliderH);
        slab.volume.render(R);
    }

    // Add button.
    SDL_FRect addBtn{NODE_W / 2.f - 40.f, kNODE_H - kFooterH - 4.f, 80.f, kFooterH};
    SDL_SetRenderDrawColor(R, 60, 100, 160, 255);
    SDL_RenderFillRect(R, &addBtn);
    const char* addLabel = "+ Add";
    SDL_Surface* addSurf = TTF_RenderText_Blended(fonts.mainFont, addLabel, strlen(addLabel), {255, 255, 255, 255});
    if (addSurf) {
        SDL_Texture* addTex = SDL_CreateTextureFromSurface(R, addSurf);
        SDL_FRect ar{addBtn.x + (addBtn.w - addSurf->w) / 2.f, addBtn.y + (addBtn.h - addSurf->h) / 2.f,
                     static_cast<float>(addSurf->w), static_cast<float>(addSurf->h)};
        SDL_RenderTexture(R, addTex, nullptr, &ar);
        SDL_DestroySurface(addSurf);
        SDL_DestroyTexture(addTex);
    }

    // Drop indicator.
    if (draggingSample_ >= 0 && dragTargetInsert_ >= 0 && dragTargetInsert_ <= static_cast<int>(samples.size())) {
        float ix = columnLeft(static_cast<size_t>(dragTargetInsert_), scrollOffset_) - 2.f;
        SDL_SetRenderDrawColor(R, 255, 200, 50, 255);
        SDL_RenderLine(R, ix, listTop, ix, listTop + colContentH);
    }

}

// ============================================================================
// UI: Input Handling
// ============================================================================

#ifndef __EMSCRIPTEN__
// Pick an audio file, or return "" if cancelled.
static std::string pickAudioFile() {
    FILE* fp = popen(
        "zenity --file-selection --title='Load Audio Sample' "
        "--file-filter='Audio Files (*.wav *.ogg *.flac *.aiff) | *.wav *.WAV *.ogg *.OGG *.flac *.FLAC *.aiff *.AIFF' "
        "2>/dev/null", "r");
    if (!fp) return "";
    char buf[4096]{};
    std::string result;
    if (fgets(buf, sizeof(buf), fp)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(fp);
    return result;
}
#endif

void SamplerNode::openSampleFileDialog() {
#ifndef __EMSCRIPTEN__
    const std::string result = pickAudioFile();

    if (!result.empty()) {
        if (project && project->um) {
            std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
            project->um->newAction(new SamplerAddSampleUndoAction(project, std::move(mp),
                static_cast<int>(id), result));
        } else {
            addSample(result);
        }
    }
#endif
}

bool SamplerNode::handleCustomInput(SDL_Event& e) {
    if (e.type == SDL_EVENT_DROP_FILE) {
        if (e.drop.data) {
            std::string path(e.drop.data);
            if (project && project->um) {
                std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                project->um->newAction(new SamplerAddSampleUndoAction(project, std::move(mp),
                    static_cast<int>(id), path));
            } else {
                addSample(path);
            }
            return true;
        }
    }

    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return false;

    float mx = msX;
    float my = msY;
    float listTop = kListTop + 24.f;

    // --- Add button ---
    SDL_FRect addBtn{NODE_W / 2.f - 40.f, kNODE_H - kFooterH - 4.f, 80.f, kFooterH};
    if (mx >= addBtn.x && mx <= addBtn.x + addBtn.w && my >= addBtn.y && my <= addBtn.y + addBtn.h) {
        if (e.button.button == SDL_BUTTON_LEFT) {
            if (project && project->um) {
                std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                project->um->newAction(new SamplerAddSampleUndoAction(project, std::move(mp),
                    static_cast<int>(id), ""));
            } else {
                addSample("");
            }
            return true;
        }
    }

    // --- Sample columns ---
    float colContentH = kThumbH + kRowH + 3.f * kBtnSz + 6.f + kSliderH + 4.f;
    for (size_t i = 0; i < samples.size(); ++i) {
        float cx = columnLeft(i, scrollOffset_);
        if (mx < cx || mx > cx + kColW) continue;
        float cy = listTop + 2.f;

        // Solo button (stacked above mute in the narrow column).
        const float btnX = cx + (kColW - kBtnSz) * 0.5f;
        SDL_FRect soloR{btnX, cy + kThumbH + kRowH, kBtnSz, kBtnSz};
        if (mx >= soloR.x && mx <= soloR.x + soloR.w && my >= soloR.y && my <= soloR.y + soloR.h) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (project && project->um) {
                    std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                    project->um->newAction(new SamplerToggleSampleSoloUndoAction(project, std::move(mp),
                        static_cast<int>(id), i));
                } else {
                    samples[i]->solo = !samples[i]->solo;
                }
                return true;
            }
        }

        // Mute button.
        SDL_FRect muteR{btnX, cy + kThumbH + kRowH + kBtnSz + 2.f, kBtnSz, kBtnSz};
        if (mx >= muteR.x && mx <= muteR.x + muteR.w && my >= muteR.y && my <= muteR.y + muteR.h) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (project && project->um) {
                    std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                    project->um->newAction(new SamplerToggleSampleMuteUndoAction(project, std::move(mp),
                        static_cast<int>(id), i));
                } else {
                    samples[i]->mute = !samples[i]->mute;
                }
                return true;
            }
        }

        // Stereo output toggle [2/1].
        SDL_FRect stereoR{btnX, cy + kThumbH + kRowH + 2.f * kBtnSz + 4.f, kBtnSz, kBtnSz};
        if (mx >= stereoR.x && mx <= stereoR.x + stereoR.w && my >= stereoR.y && my <= stereoR.y + stereoR.h) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                samples[i]->stereoOut = !samples[i]->stereoOut;
                if (i < outputs.connections.size()) {
                    auto* oc = outputs.connections[i];
                    oc->minChannels = samples[i]->stereoOut ? 2 : samples[i]->numChannels;
                    oc->updateNumChannels();
                }
                if (nm) nm->markTopologyDirty();
                return true;
            }
        }

        // Volume bar (owns press/drag/undo).
        float sliderW = 10.f;
        float sliderX = cx + (kColW - sliderW) / 2.f;
        const float sliderY = cy + kThumbH + kRowH + 3.f * kBtnSz + 6.f;
        samples[i]->volume.reposition(sliderX, sliderY, sliderW, kSliderH);
        samples[i]->volume.handleInputAt(e, mx, my);
        if (samples[i]->volume.dragging()) {
            draggingVolumeIdx_ = static_cast<int>(i);
            return true;
        }

        // Right-click → context menu.
        if (e.button.button == SDL_BUTTON_RIGHT) {
            auto* ctx = ContextMenu::get();
            ctx->activate();
            auto root = uTreeEntry();
            auto del = uTreeEntry();
            del->label = "Delete Sample";
            size_t si = i;
            del->click = [this, si]() {
                if (project && project->um) {
                    std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                    project->um->newAction(new SamplerRemoveSampleUndoAction(project, std::move(mp),
                        static_cast<int>(id), si));
                } else {
                    removeSample(si);
                }
            };
            root->addChild(del);
            auto reload = uTreeEntry();
            reload->label = "Reload File";
            reload->click = [this, si]() {
                if (!samples[si]->filepath.empty()) {
                    if (project && project->um) {
                        auto& s = *samples[si];
                        std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                        project->um->newAction(new SamplerLoadSampleFileUndoAction(project, std::move(mp),
                            static_cast<int>(id), si,
                            s.filepath, s.filepath,
                            s.sampleRate, s.numChannels,
                            s.sampleRate, s.numChannels));
                    } else {
                        setSampleFile(si, samples[si]->filepath);
                    }
                }
            };
            root->addChild(reload);
            ctx->dynamicTick = getTreeMenuTicker(root);
            return true;
        }

        // Left-click on column background → start drag-to-reorder OR open editor.
        if (e.button.button == SDL_BUTTON_LEFT) {
            draggingSample_ = static_cast<int>(i);
            dragStartX_ = mx;
            dragStartY_ = my;
            dragTargetInsert_ = static_cast<int>(i);
            return true;
        }
    }

    return false;
}

void SamplerNode::handleWindowInput(SDL_Event& e) {
    float mx = msX;
    float my = msY;
    float listTop = kListTop + 24.f;

    // Right-click on a parameter opens its context menu, in the node or the editor window.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
        float rx = mx, ry = my;
        if (window && e.button.windowID == SDL_GetWindowID(window)) {
            int wx, wy; SDL_GetWindowPosition(window, &wx, &wy);
            float gx, gy; SDL_GetGlobalMouseState(&gx, &gy);
            rx = gx - static_cast<float>(wx);
            ry = gy - static_cast<float>(wy);
        }
        if (openParameterMenuAt(e, rx, ry)) return;
    }

    // --- Drag-to-reorder ---
    if (draggingSample_ >= 0) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            int from = draggingSample_;
            int to = dragTargetInsert_;
            draggingSample_ = -1;
            dragTargetInsert_ = -1;
            if (to < 0) to = 0;
            if (to >= static_cast<int>(samples.size())) to = static_cast<int>(samples.size()) - 1;
            if (from != to && from >= 0 && to >= 0 && static_cast<size_t>(to) < samples.size()) {
                std::vector<size_t> newOrder(samples.size());
                std::iota(newOrder.begin(), newOrder.end(), static_cast<size_t>(0));
                newOrder.erase(newOrder.begin() + from);
                newOrder.insert(newOrder.begin() + to, static_cast<size_t>(from));
                if (project && project->um) {
                    std::vector<int> mp = nm ? nm->managerPath : std::vector<int>{};
                    project->um->newAction(new SamplerReorderSamplesUndoAction(project, std::move(mp),
                        static_cast<int>(id), std::move(newOrder)));
                } else {
                    reorderSamples(newOrder);
                }
            } else if (from == to) {
                // No movement → open editor (click without drag).
                openSampleEditor(static_cast<size_t>(from));
            }
            return;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            // Compute target insertion index based on mouse X.
            float relX = mx + scrollOffset_ - kColPad;
            int target = static_cast<int>(relX / (kColW + kColPad));
            if (target < 0) target = 0;
            if (target > static_cast<int>(samples.size())) target = static_cast<int>(samples.size());
            dragTargetInsert_ = target;
            return;
        }
        return;
    }

    // --- Volume bar drag (input handled by the Bar parameter) ---
    if (draggingVolumeIdx_ >= 0 && draggingVolumeIdx_ < static_cast<int>(samples.size())) {
        samples[draggingVolumeIdx_]->volume.handleInputAt(e, mx, my);
        if (!samples[draggingVolumeIdx_]->volume.dragging()) draggingVolumeIdx_ = -1;
        return;
    }

    // Hover tracking.
    if (e.type == SDL_EVENT_MOUSE_MOTION && draggingSample_ < 0 && draggingVolumeIdx_ < 0) {
        hoveredSampleIndex_ = -1;
        for (size_t i = 0; i < samples.size(); ++i) {
            float cx = columnLeft(i, scrollOffset_);
            if (mx >= cx && mx <= cx + kColW && my >= listTop && my <= listTop + kThumbH + kRowH + 3.f * kBtnSz + 6.f + kSliderH + 4.f) {
                hoveredSampleIndex_ = static_cast<int>(i);
                break;
            }
        }
    }

    // Wheel → horizontal scroll.
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        scrollOffset_ -= e.wheel.y * 30.f;
        float totalW = static_cast<float>(samples.size()) * (kColW + kColPad) - kColPad;
        float maxScroll = totalW - (NODE_W - kColPad * 2.f);
        if (maxScroll < 0.f) maxScroll = 0.f;
        if (scrollOffset_ < 0.f) scrollOffset_ = 0.f;
        if (scrollOffset_ > maxScroll) scrollOffset_ = maxScroll;
        return;
    }
}

// ============================================================================
// SampleEditorWindow
// ============================================================================

SampleEditorWindow::SampleEditorWindow(SamplerNode* node, size_t idx)
    : samplerNode(node), sampleIndex(idx) {}

void SampleEditorWindow::setSample(size_t idx) { sampleIndex = idx; }

void SampleEditorWindow::render() {
    if (!samplerNode || sampleIndex >= samplerNode->samples.size()) return;
    auto& slab = *samplerNode->samples[sampleIndex];
    SDL_Renderer* R = renderer;
    if (!R) return;

    SDL_SetRenderDrawColor(R, 40, 40, 45, 255);
    SDL_RenderClear(R);

    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);
    float ex = 8.f;

    // Filepath.
    float ey = 8.f;
    std::string fp = slab.filepath.empty() ? "(no file)" : slab.filepath;
    if (fp.length() > 38) fp = "..." + fp.substr(fp.length() - 35);
    {
        SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, fp.c_str(), fp.size(), {150, 150, 160, 255});
        if (sf) { SDL_Texture* tx = SDL_CreateTextureFromSurface(R, sf);
            SDL_FRect r{ex, ey, static_cast<float>(sf->w), static_cast<float>(sf->h)};
            SDL_RenderTexture(R, tx, nullptr, &r); SDL_DestroySurface(sf); SDL_DestroyTexture(tx); }
        ey += 18.f + 6.f;
    }

    // Waveform view with the played range shaded and the ADSR envelope on top.
    {
        const float vizW = static_cast<float>(ww) - 2.f * ex;
        const float vizH = 54.f;
        SDL_FRect viz{ex, ey, vizW, vizH};
        SDL_SetRenderDrawColor(R, 26, 26, 30, 255);
        SDL_RenderFillRect(R, &viz);

        if (slab.buffer && slab.num_samples > 0 && vizW > 4.f) {
            const size_t n = slab.num_samples;
            const int cols = static_cast<int>(vizW);
            SDL_SetRenderDrawColor(R, 90, 140, 190, 255);
            for (int c = 0; c < cols; ++c) {
                size_t s0 = n * static_cast<size_t>(c) / static_cast<size_t>(cols);
                size_t s1 = n * static_cast<size_t>(c + 1) / static_cast<size_t>(cols);
                if (s1 <= s0) s1 = s0 + 1;
                float mn = 0.f, mx = 0.f;
                for (size_t i = s0; i < s1 && i < n; ++i) {
                    const float v = slab.buffer[i];
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
                const float y0 = viz.y + vizH * 0.5f - mx * vizH * 0.5f;
                const float y1 = viz.y + vizH * 0.5f - mn * vizH * 0.5f;
                SDL_RenderLine(R, viz.x + static_cast<float>(c), y0,
                               viz.x + static_cast<float>(c), std::max(y0 + 1.f, y1));
            }
        }

        // Dim what playback skips: before `offset` and after `cutoff`.
        const float offF = std::clamp(slab.offset.value, 0.f, 1.f);
        const float cutF = std::clamp(slab.cutoff.value, 0.f, 1.f);
        SDL_SetRenderDrawColor(R, 0, 0, 0, 130);
        SDL_FRect pre{viz.x, viz.y, vizW * offF, vizH};
        SDL_RenderFillRect(R, &pre);
        SDL_FRect post{viz.x + vizW * cutF, viz.y, vizW * (1.f - cutF), vizH};
        SDL_RenderFillRect(R, &post);

        // Envelope: attack and decay in seconds, sustain held to the end of the sample.
        const float durSec = slab.sampleRate > 0
            ? static_cast<float>(slab.num_samples) / static_cast<float>(slab.sampleRate) : 1.f;
        const float atk = std::max(0.0f, slab.attack.value);
        const float dec = std::max(0.0f, slab.decay.value);
        const float sus = std::clamp(slab.sustain.value, 0.f, 1.f);
        const float invDur = 1.f / std::max(durSec, 1e-4f);
        auto exAt = [&](float sec) { return viz.x + vizW * std::clamp(sec * invDur, 0.f, 1.f); };
        auto eyAt = [&](float lvl) { return viz.y + vizH - vizH * std::clamp(lvl, 0.f, 1.f); };
        SDL_SetRenderDrawColor(R, 240, 200, 80, 230);
        SDL_RenderLine(R, exAt(0.f), eyAt(0.f), exAt(atk), eyAt(1.f));
        SDL_RenderLine(R, exAt(atk), eyAt(1.f), exAt(atk + dec), eyAt(sus));
        SDL_RenderLine(R, exAt(atk + dec), eyAt(sus), viz.x + vizW, eyAt(sus));

        SDL_SetRenderDrawColor(R, 70, 70, 78, 255);
        SDL_RenderRect(R, &viz);
    }
    ey += 54.f + 6.f;

    // Volume slider.
    char buf[64];
    snprintf(buf, sizeof(buf), "Volume: %.2f", slab.volume.value);
    SDL_Surface* sf = TTF_RenderText_Blended(fonts.mainFont, buf, strlen(buf), {200, 200, 200, 255});
    if (sf) { SDL_Texture* tx = SDL_CreateTextureFromSurface(R, sf);
        SDL_FRect r{ex, ey, static_cast<float>(sf->w), static_cast<float>(sf->h)};
        SDL_RenderTexture(R, tx, nullptr, &r); SDL_DestroySurface(sf); SDL_DestroyTexture(tx); }
    ey += 16.f;
    float sw = 200.f;
    // Volume bar (same parameter class as the node strip: owns input and undo).
    slab.volume.reposition(ex, ey, sw, 10.f);
    slab.volume.render(R);
    ey += 14.f + 8.f;

    // Knobs for the rest — reposition and render.
    float kx = 30.f, ky = ey + 22.f;
    slab.pitch.reposition(kx, ky, 14.f);   slab.pitch.render(R);
    kx += 56.f;
    slab.pan.reposition(kx, ky, 14.f);     slab.pan.render(R);
    kx += 56.f;
    slab.offset.reposition(kx, ky, 14.f);  slab.offset.render(R);
    kx += 56.f;
    slab.cutoff.reposition(kx, ky, 14.f);  slab.cutoff.render(R);
    kx = 30.f; ky += 60.f;
    slab.attack.reposition(kx, ky, 14.f);  slab.attack.render(R);
    kx += 56.f;
    slab.decay.reposition(kx, ky, 14.f);   slab.decay.render(R);
    kx += 56.f;
    slab.sustain.reposition(kx, ky, 14.f); slab.sustain.render(R);
    kx += 56.f;
    slab.release.reposition(kx, ky, 14.f); slab.release.render(R);
    ky += 60.f + 8.f;

    // Reload / Delete.
    SDL_FRect relBtn{ex, ky, 90.f, 22.f};
    SDL_SetRenderDrawColor(R, 60, 100, 160, 255); SDL_RenderFillRect(R, &relBtn);
    SDL_Surface* rs = TTF_RenderText_Blended(fonts.mainFont, "Reload", 6, {255, 255, 255, 255});
    if (rs) { SDL_Texture* rt = SDL_CreateTextureFromSurface(R, rs);
        SDL_FRect rr{relBtn.x + (relBtn.w - rs->w)/2.f, relBtn.y + (relBtn.h - rs->h)/2.f,
                     static_cast<float>(rs->w), static_cast<float>(rs->h)};
        SDL_RenderTexture(R, rt, nullptr, &rr); SDL_DestroySurface(rs); SDL_DestroyTexture(rt); }

    SDL_FRect loadBtn{ex + 100.f, ky, 90.f, 22.f};
    SDL_SetRenderDrawColor(R, 60, 140, 90, 255); SDL_RenderFillRect(R, &loadBtn);
    SDL_Surface* ls = TTF_RenderText_Blended(fonts.mainFont, "Load File", 9, {255, 255, 255, 255});
    if (ls) { SDL_Texture* lt = SDL_CreateTextureFromSurface(R, ls);
        SDL_FRect lr{loadBtn.x + (loadBtn.w - ls->w)/2.f, loadBtn.y + (loadBtn.h - ls->h)/2.f,
                     static_cast<float>(ls->w), static_cast<float>(ls->h)};
        SDL_RenderTexture(R, lt, nullptr, &lr); SDL_DestroySurface(ls); SDL_DestroyTexture(lt); }

    SDL_FRect delBtn{ex + 200.f, ky, 90.f, 22.f};
    SDL_SetRenderDrawColor(R, 180, 50, 50, 255); SDL_RenderFillRect(R, &delBtn);
    SDL_Surface* ds = TTF_RenderText_Blended(fonts.mainFont, "Delete", 6, {255, 255, 255, 255});
    if (ds) { SDL_Texture* dt = SDL_CreateTextureFromSurface(R, ds);
        SDL_FRect dr{delBtn.x + (delBtn.w - ds->w)/2.f, delBtn.y + (delBtn.h - ds->h)/2.f,
                     static_cast<float>(ds->w), static_cast<float>(ds->h)};
        SDL_RenderTexture(R, dt, nullptr, &dr); SDL_DestroySurface(ds); SDL_DestroyTexture(dt); }
}

bool SampleEditorWindow::handleInput(SDL_Event& e) {
    if (!samplerNode || sampleIndex >= samplerNode->samples.size()) return false;

    auto* p = samplerNode->project;
    auto& nm = *samplerNode->nm;
    int nid = static_cast<int>(samplerNode->id);
    auto& slab = *samplerNode->samples[sampleIndex];

    // Motion/release for an in-progress bar drag.
    if (slab.volume.dragging()) {
        int wx, wy; SDL_GetWindowPosition(window, &wx, &wy);
        float gx, gy; SDL_GetGlobalMouseState(&gx, &gy);
        slab.volume.handleInputAt(e, gx - static_cast<float>(wx), gy - static_cast<float>(wy));
        return true;
    }

    // Right-click on a knob opens its parameter menu (same one the node canvas shows).
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
        int wx, wy; SDL_GetWindowPosition(window, &wx, &wy);
        float gx, gy; SDL_GetGlobalMouseState(&gx, &gy);
        if (samplerNode->openParameterMenuAt(e, gx - static_cast<float>(wx), gy - static_cast<float>(wy)))
            return true;
    }

    // Volume bar (input + undo owned by the parameter).
    const float volY = 8.f + 24.f + 60.f + 16.f;
    slab.volume.reposition(8.f, volY, 200.f, 10.f);

    // Wheel → pass to Knob under cursor (coalescing handled by undo system).
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        int wx, wy; SDL_GetWindowPosition(window, &wx, &wy);
        float gx, gy; SDL_GetGlobalMouseState(&gx, &gy);
        int mx = static_cast<int>(gx) - wx, my = static_cast<int>(gy) - wy;
        Knob* knobs[] = {&slab.pitch, &slab.pan, &slab.offset, &slab.cutoff,
                         &slab.attack, &slab.decay, &slab.sustain, &slab.release};
        int offsets[] = {1,2,3,4,5,6,7,8};
        for (int i = 0; i < 8; ++i) {
            if (mx >= knobs[i]->knobRect.x && mx <= knobs[i]->knobRect.x + knobs[i]->knobRect.w &&
                my >= knobs[i]->knobRect.y && my <= knobs[i]->knobRect.y + knobs[i]->knobRect.h) {
                float oldVal = knobs[i]->value;
                knobs[i]->handleInput(e);
                if (knobs[i]->value != oldVal && p && p->um) {
                    std::vector<int> mp = nm.managerPath;
                    std::vector<size_t> pp{static_cast<size_t>(sampleIndex) * 9 + static_cast<size_t>(offsets[i])};
                    p->um->newAction(new SetParamValueUndoAction(p, std::move(mp), nid,
                        std::move(pp), oldVal, knobs[i]->value, knobs[i]->label));
                }
                break;
            }
        }
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        int wx, wy; SDL_GetWindowPosition(window, &wx, &wy);
        float gx, gy; SDL_GetGlobalMouseState(&gx, &gy);
        float mx = gx - static_cast<float>(wx);
        float my = gy - static_cast<float>(wy);

        // Volume bar (owns its own drag + undo).
        slab.volume.reposition(8.f, 8.f + 24.f + 60.f + 16.f, 200.f, 10.f);
        slab.volume.handleInputAt(e, mx, my);
        if (slab.volume.dragging()) return true;

        // Reload/Delete buttons.
        // Matches the render layout: filepath, waveform, volume label + track, then the
        // two knob rows (22 offset + 60 + 60) and an 8 px gap.
        float btnY = 8.f + 24.f + 60.f + 16.f + 14.f + 8.f + 22.f + 60.f + 60.f + 8.f;
        SDL_FRect relBtn{8.f, btnY, 90.f, 22.f};
        SDL_FRect loadBtn{108.f, btnY, 90.f, 22.f};
        SDL_FRect delBtn{208.f, btnY, 90.f, 22.f};
        if (mx >= loadBtn.x && mx <= loadBtn.x + loadBtn.w && my >= loadBtn.y && my <= loadBtn.y + loadBtn.h) {
#ifndef __EMSCRIPTEN__
            const std::string picked = pickAudioFile();
            if (!picked.empty()) {
                if (p && p->um) {
                    std::vector<int> mp = nm.managerPath;
                    p->um->newAction(new SamplerLoadSampleFileUndoAction(p, std::move(mp), nid,
                        sampleIndex, slab.filepath, picked,
                        slab.sampleRate, slab.numChannels, slab.sampleRate, slab.numChannels));
                } else { samplerNode->setSampleFile(sampleIndex, picked); }
            }
#endif
            return true;
        }
        if (mx >= relBtn.x && mx <= relBtn.x + relBtn.w && my >= relBtn.y && my <= relBtn.y + relBtn.h) {
            if (!slab.filepath.empty()) {
                if (p && p->um) {
                    std::vector<int> mp = nm.managerPath;
                    p->um->newAction(new SamplerLoadSampleFileUndoAction(p, std::move(mp), nid,
                        sampleIndex, slab.filepath, slab.filepath,
                        slab.sampleRate, slab.numChannels, slab.sampleRate, slab.numChannels));
                } else { samplerNode->setSampleFile(sampleIndex, slab.filepath); }
            }
            return true;
        }
        if (mx >= delBtn.x && mx <= delBtn.x + delBtn.w && my >= delBtn.y && my <= delBtn.y + delBtn.h) {
            if (p && p->um) {
                std::vector<int> mp = nm.managerPath;
                p->um->newAction(new SamplerRemoveSampleUndoAction(p, std::move(mp), nid, sampleIndex));
            } else { samplerNode->removeSample(sampleIndex); }
            onRequestClose();
            return true;
        }
    }

    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
        onRequestClose();
        return true;
    }
    return false;
}

// ============================================================================
// Serialization
// ============================================================================

json SamplerNode::extraSerialize() {
    json j;
    j["sampleIdPool"] = sampleIdPool_.toJSON();
    j["samples"] = json::array();
    for (auto& sp : samples) {
        SampleSlab& s = *sp;
        json sj;
        sj["id"] = s.id;
        sj["filepath"] = s.filepath;
        sj["solo"] = s.solo;
        sj["mute"] = s.mute;
        sj["stereoOut"] = s.stereoOut;
        sj["sampleRate"] = s.sampleRate;
        sj["numChannels"] = s.numChannels;
        sj["vol"] = s.volume.value;
        sj["pitch"] = s.pitch.value;
        sj["pan"] = s.pan.value;
        sj["offset"] = s.offset.value;
        sj["cutoff"] = s.cutoff.value;
        sj["attack"] = s.attack.value;
        sj["decay"] = s.decay.value;
        sj["sustain"] = s.sustain.value;
        sj["release"] = s.release.value;
        j["samples"].push_back(sj);
    }
    return j;
}

void SamplerNode::extraDeSerialize(const json& j) {
    if (!j.contains("samples")) return;

    if (j.contains("sampleIdPool"))
        sampleIdPool_.fromJSON(j["sampleIdPool"]);
    else if (j.contains("nextSampleId"))
        sampleIdPool_.reserveID(static_cast<uint16_t>(j["nextSampleId"].get<int>()));

    for (auto& sj : j["samples"]) {
        std::string fp = sj.value("filepath", "");
        auto slab = std::make_unique<SampleSlab>();
        slab->id = sj.value("id", -1);
        if (!fp.empty())
            slab->loadFile(fp);
        slab->solo = sj.value("solo", false);
        slab->mute = sj.value("mute", false);
        slab->stereoOut = sj.value("stereoOut", true);
        if (!slab->buffer) {
            slab->sampleRate = sj.value("sampleRate", 0);
            slab->numChannels = sj.value("numChannels", 1);
        }
        samples.push_back(std::move(slab));
        size_t idx = samples.size() - 1;
        auto& s = *samples[idx];
        if (s.id >= 0) sampleIdPool_.reserveID(static_cast<uint16_t>(s.id));
        else s.id = static_cast<int>(sampleIdPool_.newID());
        s.volume.value = sj.value("vol", 1.0f);
        s.pitch.value = sj.value("pitch", 69.0f);
        s.pan.value = sj.value("pan", 0.5f);
        s.offset.value = sj.value("offset", 0.f);
        s.cutoff.value = sj.value("cutoff", 1.f);
        s.attack.value = sj.value("attack", 0.f);
        s.decay.value = sj.value("decay", 0.f);
        s.sustain.value = sj.value("sustain", 1.f);
        s.release.value = sj.value("release", 0.f);
        s.volume.label = "S" + std::to_string(s.id) + " Vol";
        s.pitch.label = "S" + std::to_string(s.id) + " Pitch";
        s.pan.label = "S" + std::to_string(s.id) + " Pan";
        s.offset.label = "S" + std::to_string(s.id) + " Offset";
        s.cutoff.label = "S" + std::to_string(s.id) + " Cutoff";
        s.attack.label = "S" + std::to_string(s.id) + " Atk";
        s.decay.label = "S" + std::to_string(s.id) + " Dec";
        s.sustain.label = "S" + std::to_string(s.id) + " Sus";
        s.release.label = "S" + std::to_string(s.id) + " Rel";
    applySamplerKnobDisplay(s);
    }

    rebuildSampleParams();
    rebuildConnections();
}
