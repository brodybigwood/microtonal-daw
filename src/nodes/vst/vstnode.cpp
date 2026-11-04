#include "vstnode.h"
#include "JsonBytes.h"
#include "NodeManager.h"
#include "styles.h"
#include "Settings.h"
#include "UndoManager.h"
#include "NodeProcessor.h"
#include "ContextMenu.h"
#include "AudioManager.h"
#include "nodes/mixer/mixer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <SDL3_ttf/SDL_ttf.h>

// Synchronous file dialog on the calling (GUI) thread via zenity
static std::string openFileDialog() {
    FILE* fp = popen("zenity --file-selection --title='Load VST3 Plugin' 2>/dev/null", "r");
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

// ============================================================================
// VstNode constructor
// ============================================================================

// GUI-copy rack nodes, ticked once per frame to mirror master state.
static std::vector<VstNode*> g_rackNodes;

static uint64_t nowMsSteady() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

VstNode::VstNode(uint16_t id, NodeManager* nm, bool mpeRack)
    : Node(id, nm, mpeRack ? NodeType::MpeRack : NodeType::Vst), mpeRack_(mpeRack) {

    name = mpeRack ? "MPE Rack" : "VST Plugin";
    params.push_back(&bypass);

    // Create default stereo I/O — will be rebuilt when a plugin is loaded.
    rebuildConnections();
    if (mpeRack_) g_rackNodes.push_back(this);
}

void VstNode::markRackDirty() {
    rackDirty_ = true;
    rackLastEditMs_ = nowMsSteady();
}

void VstNode::tickAllRacks() {
    for (auto* n : g_rackNodes) {
        if (!n || !n->ne) continue; // GUI copy only
        if (!n->rackDirty_ || n->restoringState || !n->plugin || !n->plugin->isValid()) continue;
        auto* frame = n->plugin->getHostFrame();
        if (frame && frame->activeGestureCount > 0) continue;
        const uint64_t now = nowMsSteady();
        // Debounce: wait until edits settle, so a drag doesn't push 15 state
        // blobs per frame, then at most every 50 ms.
        if (now - n->rackLastEditMs_ < 150) continue;
        if (now - n->rackLastSyncMs_ < 50) continue;
        n->rackLastSyncMs_ = now;
        n->syncRackState();
    }
}

void VstNode::syncRackState() {
    rackDirty_ = false;
    if (!mpeRack_ || !plugin || !plugin->isValid() || rackShadows_.empty()) return;
    const auto comp = plugin->getComponentState();
    const auto ctrl = plugin->getControllerState();
    if (comp.empty() && ctrl.empty()) return;
    for (auto& shadow : rackShadows_) {
        if (!shadow || !shadow->isValid()) continue;
        shadow->beginStateRestore();
        if (!comp.empty()) shadow->setComponentState(comp);
        if (!ctrl.empty()) shadow->setControllerState(ctrl);
        shadow->endStateRestore();
    }
}

void VstNode::createRackShadows() {
    rackShadows_.clear();
    if (!mpeRack_ || !plugin || !plugin->isValid()) return;
    const std::vector<int> path = nm ? nm->managerPath : std::vector<int>{};
    for (int i = 1; i <= 15; ++i) {
        auto sp = VstPluginCache::instance().getOrCreatePlugin(
            static_cast<int>(id), path, loadedPath, classIndex_, i);
        if (sp && sp->isValid()) rackShadows_.push_back(sp);
    }
    if (std::getenv("DAW_VST_DEBUG")) {
        std::cout << "[vst-rack] node=" << id << " " << rackShadows_.size()
                  << " shadow instances of " << loadedPath << std::endl;
    }
    markRackDirty();
}

VstNode::~VstNode() {
    g_rackNodes.erase(std::remove(g_rackNodes.begin(), g_rackNodes.end(), this), g_rackNodes.end());
    if (plugin) {
        // The shared plugin (and its host frame) can outlive this copy — drop
        // the callbacks bound to `this` before they can fire again.
        if (auto* frame = plugin->getHostFrame()) {
            frame->onBeginEdit = nullptr;
            frame->onPerformEdit = nullptr;
            frame->onStatePoll = nullptr;
            frame->onStateReset = nullptr;
            frame->onGestureEnd = nullptr;
        }
        plugin->hideEditor();
    }
    plugin = nullptr;
    VstPluginCache::instance().removePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{});
}

// ============================================================================
// rebuildConnections — match plugin bus configuration
// ============================================================================

void VstNode::rebuildConnections() {
    int numAudioInBuses = 0, numAudioOutBuses = 0;
    int numEventIn = 0, numEventOut = 0;

    if (plugin && plugin->isValid()) {
        numAudioInBuses = plugin->getNumAudioInputs();
        numAudioOutBuses = plugin->getNumAudioOutputs();
        numEventIn = plugin->getNumEventInputs();
        numEventOut = plugin->getNumEventOutputs();
    }

    // Grow or reuse audio input bus connections (set numChannels before addConnection)
    for (int b = static_cast<int>(audioInConns.size()); b < numAudioInBuses; ++b) {
        int channels = plugin->getAudioInputChannels(b);
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        c->minChannels = channels;
        c->updateNumChannels();
        inputs.addConnection(c);
        audioInConns.push_back(c);
    }

    // Grow or reuse audio output bus connections (set numChannels before addConnection)
    for (int b = static_cast<int>(audioOutConns.size()); b < numAudioOutBuses; ++b) {
        int channels = plugin->getAudioOutputChannels(b);
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::output;
        c->minChannels = channels;
        c->updateNumChannels();
        outputs.addConnection(c);
        audioOutConns.push_back(c);
    }

    // Update labels for existing connections
    for (int b = 0; b < numAudioInBuses && b < static_cast<int>(audioInConns.size()); ++b) {
        int channels = plugin->getAudioInputChannels(b);
        auto* ic = audioInConns[b];
        ic->minChannels = channels;
        ic->numChannels = channels;
        ic->allocChannels = channels;
        ic->label = "In " + std::to_string(b + 1) + " (" + std::to_string(channels) + "ch)";
    }
    for (int b = numAudioInBuses; b < static_cast<int>(audioInConns.size()); ++b) {
        // Bus no longer exists on the plugin: drop any pointer it held so it can
        // never be read after the buffer it referenced is freed.
        audioInConns[b]->label = "";
        audioInConns[b]->is_connected = false;
        audioInConns[b]->buffer = nullptr;
        audioInConns[b]->sourceChannels = 0;
    }

    for (int b = 0; b < numAudioOutBuses && b < static_cast<int>(audioOutConns.size()); ++b) {
        int channels = plugin->getAudioOutputChannels(b);
        auto* c = audioOutConns[b];
        c->minChannels = channels;
        if (c->numChannels != channels)
            c->allocateBuffer(bufferSize, channels);
        c->label = "Out " + std::to_string(b + 1) + " (" + std::to_string(channels) + "ch)";
    }
    for (int b = numAudioOutBuses; b < static_cast<int>(audioOutConns.size()); ++b)
        audioOutConns[b]->label = "";

    // MIDI connections: create once, reuse
    if (numEventIn > 0 && !midiIn) {
        midiIn = new Connection;
        midiIn->type = DataType::Events;
        midiIn->dir = Direction::input;
        midiIn->label = "MIDI In";
        inputs.addConnection(midiIn);
    }
    if (numEventOut > 0 && !midiOut) {
        midiOut = new Connection;
        midiOut->type = DataType::Events;
        midiOut->dir = Direction::output;
        midiOut->label = "MIDI Out";
        outputs.addConnection(midiOut);
    }
}

// ============================================================================
// setup
// ============================================================================

void VstNode::invalidateOwnerChain() {
    if (!nm || mixerOwnerNodeID < 0 || mixerOwnerChainID < 0) return;
    auto* mixer = dynamic_cast<MixerNode*>(nm->getNode(static_cast<uint16_t>(mixerOwnerNodeID)));
    if (!mixer) return;
    if (auto* c = mixer->chainByID(mixerOwnerChainID)) c->appliedInCh = -1;
    nm->markTopologyDirty();
}

void VstNode::setup() {
    if (plugin && plugin->isValid()) {
        plugin->setup(sampleRate, bufferSize);
        for (auto& shadow : rackShadows_)
            if (shadow) shadow->setup(sampleRate, bufferSize);
        applyPendingVstState();
        captureStateBaseline();
        // Latency is often only known after setupProcessing/setActive. If it
        // changed, recompute the graph's compensation delays.
        const int lat = plugin->latencySamples();
        if (lat != lastReportedLatency_) {
            lastReportedLatency_ = lat;
            if (nm) nm->markTopologyDirty();
        }
    }
}

void VstNode::applyPendingVstState() {
    if (!plugin || !plugin->isValid()) return;
    if (pendingCompState_.empty() && pendingCtrlState_.empty()) return;

    restoringState = true;
    plugin->beginStateRestore();
    if (!pendingCompState_.empty()) plugin->setComponentState(pendingCompState_);
    if (!pendingCtrlState_.empty()) plugin->setControllerState(pendingCtrlState_);
    plugin->endStateRestore();
    restoringState = false;
    pendingCompState_.clear();
    pendingCtrlState_.clear();
    plugin->refreshParameterShadow();
    if (mpeRack_) markRackDirty();
    vstStateBaselineDirty = true;
    requestStatePollSoon();
}

void VstNode::captureStateBaseline() {
    if (!plugin || !plugin->isValid() || restoringState) return;
    // Only the GUI copy needs the baseline; the DSP copy has no editor/undo.
    if (!project || !project->processor) return;
    if (NodeProcessor::getActiveRoot() == project->processor->dspGraph) return;

    auto comp = plugin->getComponentState();
    auto ctrl = plugin->getControllerState();
    if (comp.empty() && ctrl.empty()) return;
    stateBaselineComp_ = std::move(comp);
    stateBaselineCtrl_ = std::move(ctrl);
    stateBaselineValid_ = true;
    vstStateBaselineDirty = false;
    lastStatePollMs_ = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    mappedBaselineValues_.clear();
    for (auto& [pid, conn] : mappedVstParams)
        if (conn && conn->is_connected)
            mappedBaselineValues_[pid] = plugin->getParameterValue(pid);
}

// ============================================================================
// process
// ============================================================================

void VstNode::process() {
    bool hasOut = false;
    for (auto* c : audioOutConns) {
        if (c && c->buffer) { hasOut = true; break; }
    }

    if (!hasOut) return;

    // Silence active output buses
    for (auto* c : audioOutConns) {
        if (c && c->buffer)
            std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(c->numChannels) * sizeof(float));
    }

    auto localPlugin = plugin;
    if (localPlugin && localPlugin->isValid()) {
        bool bypassed = bypass.value > 0.5f;
        if (bypassed && !wasBypassed_ && !noteChannels.empty()) {
            // Deliver releases once before bypass stops normal event processing.
            std::array<HostEventList, 16> rackReleases;
            HostEventList releases;
            auto releaseList = [&](int ch) -> HostEventList& {
                if (!mpeRack_) return releases;
                return rackReleases[static_cast<size_t>(std::clamp(ch, 0, 15))];
            };
            for (const auto& [noteId, channel] : noteChannels) {
                Steinberg::Vst::Event off{};
                off.busIndex = 0;
                off.sampleOffset = 0;
                off.flags = 0;
                off.type = Steinberg::Vst::Event::kNoteOffEvent;
                off.noteOff.channel = static_cast<Steinberg::int16>(channel);
                off.noteOff.pitch = 0;
                off.noteOff.tuning = 0;
                off.noteOff.velocity = 0.0f;
                off.noteOff.noteId = noteId;
                releaseList(channel).addEvent(off);
            }
            localPlugin->setBypassed(false);
            double beat = project ? project->activeBeatPosition() : 0.0;
            double seconds = project ? project->activeTimeSeconds() : 0.0;
            double bpm = project
                ? static_cast<double>(project->activeTempoCurve().evaluateAtX(static_cast<float>(beat)))
                : 120.0;
            double sr = AudioManager::instance()->sampleRate;
            if (mpeRack_) {
                for (size_t i = 0; i < rackShadows_.size() && i < 15; ++i) {
                    auto& sh = rackShadows_[i];
                    if (!sh || !sh->isValid()) continue;
                    HostEventList& evs = rackReleases[i + 1];
                    sh->processAudio(bufferSize, evs.events.empty() ? nullptr : &evs,
                                     bpm, beat, seconds * sr,
                                     project ? project->dspIsPlaying : true);
                }
            } else {
                localPlugin->processAudio(bufferSize, &releases, bpm, beat, seconds * sr,
                                          project ? project->dspIsPlaying : true);
            }
            noteChannels.clear();
            evictedNoteChannels.clear();
        }
        wasBypassed_ = bypassed;
        localPlugin->setBypassed(bypassed);
        if (!bypassed) {
            auto applyMapped = [&](VstPlugin* p) {
                for (auto& [paramID, conn] : mappedVstParams) {
                    if (conn && conn->is_connected && conn->buffer) {
                        auto v = static_cast<float>(std::clamp(conn->buffer[0] * 0.5f + 0.5f, 0.0f, 1.0f));
                        p->setParameterValue(paramID, v);
                    }
                }
            };
            auto copyInputs = [&](VstPlugin* p) {
                for (size_t bi = 0; bi < audioInConns.size(); ++bi) {
                    auto* c = audioInConns[bi];
                    if (bi < p->inputBusData.size() &&
                        (!c || !c->is_connected || !c->buffer)) {
                        // Unfed bus: no stale samples from an earlier block.
                        std::fill(p->inputBusData[bi].begin(), p->inputBusData[bi].end(), 0.0f);
                        continue;
                    }
                    if (!c || !c->is_connected || !c->buffer) continue;
                    if (bi >= p->inputBusData.size()) continue;
                    auto& data = p->inputBusData[bi];
                    const size_t busCh = data.size() / static_cast<size_t>(bufferSize);
                    const size_t copyCh = std::min(static_cast<size_t>(c->numChannels), busCh);
                    std::memcpy(data.data(), c->buffer,
                                static_cast<size_t>(bufferSize) * copyCh * sizeof(float));
                }
            };

            applyMapped(localPlugin.get());
            copyInputs(localPlugin.get());

            // Convert DAW Events to VST3 EventList with MPE. In rack mode each
            // member channel gets its own list fed to its own instance.
            std::array<HostEventList, 16> channelEvents;
            HostEventList vstEvents;
            auto eventList = [&](int ch) -> HostEventList& {
                if (!mpeRack_) return vstEvents;
                return channelEvents[static_cast<size_t>(std::clamp(ch, 0, 15))];
            };
            if (mpeRack_) {
                if (!mpeRangeSet) {
                    mpeRangeSet = true;
                    auto rpn = [](HostEventList& out, int ch, int cc, int val) {
                        Steinberg::Vst::Event ev{};
                        ev.busIndex = 0;
                        ev.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
                        ev.midiCCOut.controlNumber = static_cast<Steinberg::uint8>(cc);
                        ev.midiCCOut.channel = static_cast<Steinberg::int8>(ch);
                        ev.midiCCOut.value = static_cast<Steinberg::int8>(val);
                        ev.midiCCOut.value2 = 0;
                        out.addEvent(ev);
                    };
                    for (int ch = 1; ch <= 15; ++ch) {
                        HostEventList& out = channelEvents[static_cast<size_t>(ch)];
                        rpn(out, ch, 101, 0); rpn(out, ch, 100, 0);
                        rpn(out, ch, 6, 48);  rpn(out, ch, 38, 0);
                        rpn(out, ch, 101, 127); rpn(out, ch, 100, 127);
                    }
                }
            } else {
                sendMpePitchBendRange(&vstEvents);
            }
            const bool midiConnected = midiIn && midiIn->is_connected && midiIn->events;
            if (!midiConnected && !noteChannels.empty()) {
                // The upstream event connection disappeared while voices were
                // active. Release them before forgetting the MPE allocation.
                for (const auto& [noteId, channel] : noteChannels) {
                    Steinberg::Vst::Event off{};
                    off.busIndex = 0;
                    off.sampleOffset = 0;
                    off.flags = 0;
                    off.type = Steinberg::Vst::Event::kNoteOffEvent;
                    off.noteOff.channel = static_cast<Steinberg::int16>(channel);
                    off.noteOff.pitch = 0;
                    off.noteOff.tuning = 0;
                    off.noteOff.velocity = 0.0f;
                    off.noteOff.noteId = noteId;
                    eventList(channel).addEvent(off);
                }
                noteChannels.clear();
                evictedNoteChannels.clear();
            }
            if (midiConnected) {
                for (auto& ev : *midiIn->events) {
                    Steinberg::int16 base = static_cast<Steinberg::int16>(std::floor(ev.num));
                    float frac = ev.num - base;

                    if (ev.type == noteOn) {
                        int mpeCh = allocMpeChannel(ev.id, &vstEvents, mpeRack_ ? &channelEvents : nullptr);
                        if (mpeCh > 0) {
                            // MPE standard pitch-bend range is ±48 semitones. Bend by
                            // `frac` semitones in that range (the RPN below tells the
                            // plugin the range; 48 is the spec default MPE plugins use).
                            Steinberg::int32 pb14 = 8192;
                            if (frac != 0.0f) {
                                pb14 = 8192 + static_cast<Steinberg::int32>(frac / 48.0f * 8192.0f);
                                pb14 = std::max(0, std::min(16383, static_cast<int>(pb14)));
                            }
                            Steinberg::Vst::Event pb{};
                            pb.busIndex = 0;
                            pb.sampleOffset = ev.sampleOffset;
                            pb.flags = Steinberg::Vst::Event::kIsLive;
                            pb.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
                            pb.midiCCOut.controlNumber = Steinberg::Vst::kPitchBend;
                            pb.midiCCOut.channel = static_cast<Steinberg::int8>(mpeCh);
                            pb.midiCCOut.value = static_cast<Steinberg::int8>(pb14 & 0x7F);
                            pb.midiCCOut.value2 = static_cast<Steinberg::int8>((pb14 >> 7) & 0x7F);
                            eventList(mpeCh).addEvent(pb);
                        }

                        Steinberg::Vst::Event e{};
                        e.busIndex = 0;
                        e.sampleOffset = ev.sampleOffset;
                        e.flags = Steinberg::Vst::Event::kIsLive;
                        e.type = Steinberg::Vst::Event::kNoteOnEvent;
                        e.noteOn.channel = static_cast<Steinberg::int16>(mpeCh);
                        e.noteOn.pitch = base;
                        e.noteOn.tuning = 0;
                        e.noteOn.velocity = 1.0f;
                        e.noteOn.noteId = ev.id;
                        eventList(mpeCh).addEvent(e);
                    } else if (ev.type == noteOff) {
                        int mpeCh = -1;
                        auto it = noteChannels.find(ev.id);
                        if (it != noteChannels.end()) {
                            mpeCh = it->second;
                            freeMpeChannel(ev.id);
                        } else {
                            auto eit = evictedNoteChannels.find(ev.id);
                            if (eit != evictedNoteChannels.end()) {
                                mpeCh = eit->second;
                                evictedNoteChannels.erase(eit);
                            }
                        }
                        if (mpeCh < 0) continue;

                        Steinberg::Vst::Event e{};
                        e.busIndex = 0;
                        e.sampleOffset = ev.sampleOffset;
                        e.flags = 0;
                        e.type = Steinberg::Vst::Event::kNoteOffEvent;
                        e.noteOff.channel = static_cast<Steinberg::int16>(mpeCh >= 0 ? mpeCh : 0);
                        e.noteOff.pitch = base;
                        e.noteOff.tuning = 0;
                        e.noteOff.velocity = 0.0f;
                        e.noteOff.noteId = ev.id;
                        eventList(mpeCh).addEvent(e);
                    }
                }
            }

            double bpm = 120.0;
            double beatPos = project ? project->activeBeatPosition() : 0.0;
            double samplePos = (project && AudioManager::instance())
                ? project->activeTimeSeconds() * AudioManager::instance()->sampleRate : 0.0;
            if (project) {
                if (project->dspIsPlaying) {
                    double db = project->deltaBeats;
                    double dt = static_cast<double>(bufferSize) / AudioManager::instance()->sampleRate;
                    bpm = (dt > 0.0) ? db * 60.0 / dt : 120.0;
                } else {
                    bpm = static_cast<double>(project->activeTempoCurve().evaluateAtX(
                        static_cast<float>(project->activeBeatPosition())));
                }
            }
            bool playing = project ? project->dspIsPlaying : true;
            if (!mpeRack_) {
                const auto procStart = std::chrono::steady_clock::now();
                localPlugin->processAudio(bufferSize, &vstEvents, bpm, beatPos, samplePos, playing);
                const auto procEnd = std::chrono::steady_clock::now();
                const long long procUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    procEnd - procStart).count();
                const int lat = localPlugin->latencySamples();
                if (lat != lastReportedLatency_) {
                    lastReportedLatency_ = lat;
                    if (nm) nm->markTopologyDirty();
                }
                if (std::getenv("DAW_VST_DEBUG")) {
                    const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        procEnd.time_since_epoch()).count());
                    if (nowMs - lastProcessLogMs_ >= 1000) {
                        lastProcessLogMs_ = nowMs;
                        const long long deadlineUs = static_cast<long long>(bufferSize) * 1000000LL /
                                                     (sampleRate > 0 ? sampleRate : 48000);
                        if (procUs > deadlineUs / 2) {
                            std::cout << "[vst-time] node=" << id << " " << localPlugin->getName()
                                      << " process=" << procUs << "us deadline=" << deadlineUs
                                      << "us ch=" << localPlugin->outputBusData.size() << std::endl;
                        }
                    }
                }

                // Route audio output buses directly (both sides planar)
                for (size_t bi = 0; bi < audioOutConns.size(); ++bi) {
                    auto* c = audioOutConns[bi];
                    if (!c || !c->buffer) continue;
                    if (bi >= localPlugin->outputBusData.size()) continue;
                    auto& data = localPlugin->outputBusData[bi];
                    size_t ch = std::min(static_cast<size_t>(c->numChannels), data.size() / static_cast<size_t>(bufferSize));
                    std::memcpy(c->buffer, data.data(), static_cast<size_t>(bufferSize) * ch * sizeof(float));
                }
            } else {
                // MPE rack: one instance per member channel, summed into the
                // node's outputs. Instances with no notes (and no recent tail)
                // are skipped so sixteen copies don't cost sixteen times the CPU.
                const uint64_t nowMs = SDL_GetTicks();
                long long worstUs = 0;
                for (size_t i = 0; i < rackShadows_.size() && i < 15; ++i) {
                    auto& sh = rackShadows_[i];
                    if (!sh || !sh->isValid()) continue;
                    const int ch = static_cast<int>(i) + 1;
                    HostEventList& evs = channelEvents[static_cast<size_t>(ch)];
                    bool channelHasNotes = false;
                    for (auto& [noteId, noteCh] : noteChannels)
                        if (noteCh == ch) { channelHasNotes = true; break; }
                    if (channelHasNotes || !evs.events.empty())
                        rackChannelLastMs_[static_cast<size_t>(ch)] = nowMs;
                    const bool live = channelHasNotes || !evs.events.empty() ||
                                      (nowMs - rackChannelLastMs_[static_cast<size_t>(ch)] < 5000);
                    if (!live) continue;

                    applyMapped(sh.get());
                    copyInputs(sh.get());
                    const auto t0 = std::chrono::steady_clock::now();
                    sh->processAudio(bufferSize, evs.events.empty() ? nullptr : &evs,
                                     bpm, beatPos, samplePos, playing);
                    worstUs = std::max<long long>(worstUs, std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - t0).count());

                    for (size_t bi = 0; bi < audioOutConns.size(); ++bi) {
                        auto* c = audioOutConns[bi];
                        if (!c || !c->buffer) continue;
                        if (bi >= sh->outputBusData.size()) continue;
                        auto& data = sh->outputBusData[bi];
                        const size_t chn = std::min(static_cast<size_t>(c->numChannels),
                                                    data.size() / static_cast<size_t>(bufferSize));
                        const size_t n = static_cast<size_t>(bufferSize) * chn;
                        for (size_t k = 0; k < n; ++k) c->buffer[k] += data[k];
                    }
                }
                const int lat = localPlugin->latencySamples();
                if (lat != lastReportedLatency_) {
                    lastReportedLatency_ = lat;
                    if (nm) nm->markTopologyDirty();
                }
                if (std::getenv("DAW_VST_DEBUG") && nowMs - lastProcessLogMs_ >= 1000) {
                    lastProcessLogMs_ = nowMs;
                    const long long deadlineUs = static_cast<long long>(bufferSize) * 1000000LL /
                                                 (sampleRate > 0 ? sampleRate : 48000);
                    float outPeak = 0.0f;
                    for (auto* c : audioOutConns)
                        if (c && c->buffer)
                            for (int i = 0; i < bufferSize * std::max(1, c->numChannels); ++i)
                                outPeak = std::max(outPeak, std::fabs(c->buffer[i]));
                    int eventCount = 0;
                    for (int ch = 1; ch <= 15; ++ch)
                        eventCount += static_cast<int>(channelEvents[static_cast<size_t>(ch)].events.size());
                    if (worstUs > deadlineUs / 2 || outPeak > 0.0f || eventCount > 0) {
                        std::cout << "[vst-rack] node=" << id << " playing=" << playing
                                  << " notes=" << noteChannels.size() << " events=" << eventCount
                                  << " outPeak=" << outPeak << " worst=" << worstUs
                                  << "us deadline=" << deadlineUs << "us" << std::endl;
                    }
                }
            }
        } else {
            // Bypass: passthrough per bus
            for (size_t bi = 0; bi < audioOutConns.size(); ++bi) {
                auto* outC = audioOutConns[bi];
                if (!outC || !outC->buffer) continue;
                size_t copyCh = static_cast<size_t>(outC->numChannels);
                auto* inC = bi < audioInConns.size() ? audioInConns[bi] : nullptr;
                if (inC && inC->is_connected)
                    copyCh = std::min(copyCh, static_cast<size_t>(inC->numChannels));
                size_t busSamples = static_cast<size_t>(bufferSize) * copyCh;
                if (inC && inC->is_connected)
                    std::memcpy(outC->buffer, inC->buffer, busSamples * sizeof(float));
                else
                    std::memset(outC->buffer, 0, busSamples * sizeof(float));
            }
        }
    } else {
        // No plugin: passthrough per bus
        for (size_t bi = 0; bi < audioOutConns.size(); ++bi) {
            auto* outC = audioOutConns[bi];
            if (!outC || !outC->buffer) continue;
            size_t copyCh = static_cast<size_t>(outC->numChannels);
            auto* inC = bi < audioInConns.size() ? audioInConns[bi] : nullptr;
            if (inC && inC->is_connected)
                copyCh = std::min(copyCh, static_cast<size_t>(inC->numChannels));
            size_t busSamples = static_cast<size_t>(bufferSize) * copyCh;
            if (inC && inC->is_connected)
                std::memcpy(outC->buffer, inC->buffer, busSamples * sizeof(float));
            else
                std::memset(outC->buffer, 0, busSamples * sizeof(float));
        }
    }
}

// ============================================================================
// renderContent
// ============================================================================

void VstNode::renderContent(SDL_Renderer* renderer) {
#ifdef __EMSCRIPTEN__
    if (fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont,
            "VST not supported on browsers", 0, SDL_Color{220, 220, 220, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_FRect tr{10, 10, static_cast<float>(surf->w), static_cast<float>(surf->h)};
            SDL_RenderTexture(renderer, tex, nullptr, &tr);
            SDL_DestroyTexture(tex);
            SDL_DestroySurface(surf);
        }
    }
    return;
#else

    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];
        vx[0] = 0; vx[1] = NODE_W; vx[2] = NODE_W; vx[3] = 0;
        vy[0] = 0; vy[1] = 0; vy[2] = NODE_H; vy[3] = NODE_H;
    }

    SDL_SetRenderDrawColor(renderer, mpeRack_ ? 20 : 24, mpeRack_ ? 34 : 24, mpeRack_ ? 28 : 28, 255);
    SDL_FRect full{0, 0, NODE_W, NODE_H};
    SDL_RenderFillRect(renderer, &full);
    if (mpeRack_) {
        // Accent strip + badge so an MPE rack is never mistaken for a plain VST.
        SDL_SetRenderDrawColor(renderer, 70, 190, 120, 255);
        SDL_FRect strip{0, 0, NODE_W, 3};
        SDL_RenderFillRect(renderer, &strip);
    }

    if (!fonts.mainFont) return;

    auto renderText = [&](const std::string& text, float x, float y, SDL_Color color) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text.c_str(), 0, color);
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_FRect tr{x, y, static_cast<float>(surf->w), static_cast<float>(surf->h)};
        SDL_RenderTexture(renderer, tex, nullptr, &tr);
        SDL_DestroyTexture(tex);
        SDL_DestroySurface(surf);
    };

    SDL_Color white{220, 220, 220, 255};
    SDL_Color grey{150, 150, 155, 255};

    // Dropdown button
    std::string displayName = plugin ? plugin->getName() : "Select VST...";
    if (displayName.empty()) displayName = "(unnamed)";
    if (displayName.size() > 22) displayName = displayName.substr(0, 22) + "...";

    bool ddHover = (msX >= dropdownRect_.x && msX < dropdownRect_.x + dropdownRect_.w &&
                    msY >= dropdownRect_.y && msY < dropdownRect_.y + dropdownRect_.h);
    SDL_SetRenderDrawColor(renderer, ddHover ? 48 : 38, ddHover ? 48 : 38, ddHover ? 52 : 42, 255);
    SDL_RenderFillRect(renderer, &dropdownRect_);
    SDL_SetRenderDrawColor(renderer, 70, 70, 74, 255);
    SDL_RenderRect(renderer, &dropdownRect_);
    renderText(displayName, dropdownRect_.x + 6, dropdownRect_.y + 4, white);

    // Dropdown arrow
    float ax = dropdownRect_.x + dropdownRect_.w - 12.f;
    float ay = dropdownRect_.y + 8.f;
    SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
    SDL_RenderLine(renderer, ax, ay, ax + 4, ay + 6);
    SDL_RenderLine(renderer, ax + 4, ay + 6, ax + 8, ay);

    // Editor toggle button
    if (plugin && plugin->isValid()) {
        bool hovered = (msX >= editorBtnRect_.x && msX < editorBtnRect_.x + editorBtnRect_.w &&
                        msY >= editorBtnRect_.y && msY < editorBtnRect_.y + editorBtnRect_.h);
        SDL_SetRenderDrawColor(renderer, hovered ? 65 : 50, hovered ? 65 : 50, hovered ? 70 : 55, 255);
        SDL_RenderFillRect(renderer, &editorBtnRect_);
        SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
        SDL_RenderRect(renderer, &editorBtnRect_);
        const char* label = plugin->isEditorOpen() ? "Hide Editor" : "Show Editor";
        renderText(label, editorBtnRect_.x + 6, editorBtnRect_.y + 4, white);
    }

    if (mpeRack_) {
        const std::string tag = "MPE RACK x" + std::to_string(rackShadows_.size() + 1);
        renderText(tag, 8, 66, SDL_Color{120, 225, 150, 255});
    }

    // Editor run-loop ticking happens globally once per frame
    // (VstPlugin::tickAllEditors from NodeEditor::tick). Ticking here made the
    // mixer-chain preview flicker whenever the plugin editor was open.
#endif
}

// ============================================================================
// handleWindowInput
// ============================================================================

void VstNode::handleWindowInput(SDL_Event& e) {
    Node::handleWindowInput(e);
}

// ============================================================================
// handleCustomInput
// ============================================================================

bool VstNode::handleCustomInput(SDL_Event& e) {
#ifdef __EMSCRIPTEN__
    return false;
#endif

    if (e.type == SDL_EVENT_DROP_FILE) {
        const char* path = e.drop.data;
        if (path) {
            std::cout << "[VstNode] drop file: " << path << std::endl;
            loadPlugin(path, true);
            return true;
        }
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        SDL_FPoint pt{msX, msY};

        // Editor button click
        if (SDL_PointInRectFloat(&pt, &editorBtnRect_)) {
            if (plugin && plugin->isValid()) {
                if (plugin->isEditorOpen()) {
                    // Catch edits made since the last poll before the editor goes away.
                    pollVstStateForUndo(true);
                    plugin->hideEditor();
                } else {
                    plugin->showEditor();
                    wirePluginCallbacks();
                }
            }
            return true;
        }

        // Dropdown: open context menu with history
        if (SDL_PointInRectFloat(&pt, &dropdownRect_)) {
            auto* ctxMenu = ContextMenu::get();
            ctxMenu->activate();

            auto root = uTreeEntry();
            root->label = "VST Plugin";
            root->maxListHeight = 300.f;

            auto history = Settings::instance().getVstHistory();
            for (const auto& p : history) {
                std::string name = p;
                size_t slash = name.find_last_of("/\\");
                if (slash != std::string::npos) name = name.substr(slash + 1);
                size_t dot = name.find_last_of('.');
                if (dot != std::string::npos) name = name.substr(0, dot);
                if (name.size() > 35) name = name.substr(0, 35) + "...";

                auto item = uTreeEntry();
                item->label = name;
                item->click = [this, p]() { openPluginClassMenu(p, true); };
                root->addChild(item);
            }

            auto loadFile = uTreeEntry();
            loadFile->label = "Load from file...";
            loadFile->click = [this]() {
                std::string path = openFileDialog();
                if (!path.empty()) openPluginClassMenu(path, true);
            };
            root->addChild(loadFile);

            ctxMenu->dynamicTick = getTreeMenuTicker(root);
            return true;
        }
    }

    return false;
}

// ============================================================================
// loadPlugin / unloadPlugin
// ============================================================================

void VstNode::loadPlugin(const std::string& path, bool createUndo, size_t classIndex) {
    const bool pathChanged = path != loadedPath;
    const size_t newClassIndex = (classIndex != static_cast<size_t>(-1))
        ? classIndex : (pathChanged ? 0 : classIndex_);

    if (createUndo) {
        // --- GUI thread: create/destroy the shared instance ---
        json oldState = json::object();
        if (plugin && plugin->isValid()) {
            oldState["path"] = loadedPath;
            oldState["classIndex"] = classIndex_;
            oldState["bypass"] = bypass.value;
            oldState["compState"] = jsonBytesEncode(plugin->getComponentState());
            oldState["ctrlState"] = jsonBytesEncode(plugin->getControllerState());
        }

        if (plugin) {
            if (ne != nullptr) plugin->hideEditor();
            plugin = nullptr;
        }
        VstPluginCache::instance().removePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{});

        classIndex_ = newClassIndex;
        loadedPath = path;
        plugin = VstPluginCache::instance().getOrCreatePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{}, path, classIndex_);
        if (plugin) plugin->setUndoStatePoll(loadedPath.find("lsp-plugins") != std::string::npos);
        if (plugin && plugin->isValid()) {
            name = plugin->getName();
            className_ = plugin->getClassName();
            wirePluginCallbacks();
            plugin->refreshParameterShadow();
            rebuildConnections();
            if (mpeRack_) createRackShadows();
            invalidateOwnerChain();
            nm->markTopologyDirty(); // force setup() via update() on next audio cycle
            Settings::instance().addVstToHistory(path);
            // Don't call setup() on GUI — audio thread handles it to avoid races
        } else {
            name = "VST Plugin";
            plugin = nullptr;
            rackShadows_.clear();
            rebuildConnections();
            invalidateOwnerChain();
            nm->markTopologyDirty();
        }

        if (project && project->um) {
            std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
            json newState = json::object();
            newState["path"] = loadedPath;
            newState["classIndex"] = classIndex_;
            newState["bypass"] = bypass.value;
            if (plugin && plugin->isValid()) {
                newState["compState"] = jsonBytesEncode(plugin->getComponentState());
                newState["ctrlState"] = jsonBytesEncode(plugin->getControllerState());
            }
            auto* pa = new VstLoadPluginAction(project, std::move(mgrPath), static_cast<int>(id),
                                                std::move(oldState), std::move(newState));
            project->um->newAction(pa);
        }
    } else {
        // --- Undo/audio thread: retrieve existing instance, don't destroy ---
        std::cout << "[VstNode::loadPlugin] sync-path nodeID=" << static_cast<int>(id)
                  << " path=" << path << " ne=" << (ne ? "gui" : "audio") << std::endl;
        if (plugin) plugin->hideEditor();
        classIndex_ = newClassIndex;
        loadedPath = path;
        plugin = VstPluginCache::instance().getOrCreatePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{}, path, classIndex_);
        if (plugin) plugin->setUndoStatePoll(loadedPath.find("lsp-plugins") != std::string::npos);
        if (plugin && plugin->isValid()) {
            name = plugin->getName();
            className_ = plugin->getClassName();
            wirePluginCallbacks();
            plugin->refreshParameterShadow();
            rebuildConnections();
            if (mpeRack_) createRackShadows();
            invalidateOwnerChain();
            // rebuildConnections reallocated this node's output buffers. Downstream
            // connections (including a mixer's chain inflow) still point at the old
            // blocks until the graph relinks, so force that before the next block.
            nm->markTopologyDirty();
            Settings::instance().addVstToHistory(path);
            int sr = sampleRate > 0 ? sampleRate : 48000;
            int bs = bufferSize > 0 ? bufferSize : 1024;
            plugin->setup(sr, bs);
        } else {
            name = "VST Plugin";
            plugin = nullptr;
            rackShadows_.clear();
            rebuildConnections();
            invalidateOwnerChain();
            nm->markTopologyDirty();
        }
    }
}

void VstNode::openPluginClassMenu(const std::string& path, bool createUndo) {
    PlugLib* lib = VstPluginCache::instance().load(path);
    if (!lib || lib->classCount() <= 1) {
        loadPlugin(path, createUndo, 0);
        return;
    }

    auto* ctxMenu = ContextMenu::get();
    ctxMenu->activate();
    auto root = uTreeEntry();
    root->label = "Plugin Class";
    // Bundles such as LSP expose 100+ classes; the list must scroll.
    root->maxListHeight = 300.f;
    for (size_t i = 0; i < lib->classCount(); ++i) {
        const PlugLib::ClassInfo* cls = lib->getClass(i);
        if (!cls) continue;
        auto entry = uTreeEntry();
        entry->label = cls->name;
        entry->click = [this, path, i, createUndo]() { loadPlugin(path, createUndo, i); };
        root->addChild(entry);
    }
    ctxMenu->dynamicTick = getTreeMenuTicker(root);
}

void VstNode::unloadPlugin() {
    if (plugin) {
        plugin->hideEditor();
        plugin = nullptr;
    }
    VstPluginCache::instance().removePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{});
    rackShadows_.clear();
    loadedPath.clear();
    classIndex_ = 0;
    className_.clear();
    name = "VST Plugin";
    rebuildConnections();
}

void VstNode::onPluginParameterChange(int paramID, float oldValue, float newValue, uint64_t gestureID) {
    if (!project || !project->um || !plugin) return;
    if (restoringState) return;
    if (mpeRack_) markRackDirty();
    // Suppress undo when a mapped connection owns the parameter.
    auto it = mappedVstParams.find(paramID);
    if (it != mappedVstParams.end() && it->second && it->second->is_connected)
        return;
    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
    // Coalesce edits that belong to the same beginEdit..endEdit gesture (a knob
    // drag). Separate gestures are separate undo steps even on the same param.
    if (gestureID != 0 && project->um->current && project->um->current->type == VstParameterChange) {
        auto* prev = static_cast<VstParameterChangeAction*>(project->um->current);
        if (prev->nodeID == static_cast<int>(id)
            && prev->paramID == static_cast<uint32_t>(paramID)
            && prev->gestureID == gestureID
            && prev->managerPath == mgrPath) {
            prev->newValue = newValue;
            ProjectAction* cap = prev;
            project->um->enqueueAudioSync([cap]() { cap->doAction(); });
            vstStateBaselineDirty = true;
            return;
        }
    }
    if (oldValue == newValue) return;
    auto* pa = new VstParameterChangeAction(project, std::move(mgrPath), static_cast<int>(id),
                                             static_cast<uint32_t>(paramID), oldValue, newValue,
                                             gestureID);
    project->um->newAction(pa);
    vstStateBaselineDirty = true;
    requestStatePollSoon();
}

void VstNode::wirePluginCallbacks() {
    if (!plugin || !plugin->isValid()) return;
    auto* frame = plugin->getHostFrame();
    if (!frame) return;
    mpeRangeSet = false;
    // Ardour's VST3PI: performEdit updates the parameter shadow and is the undo
    // signal; beginEdit/endEdit only delimit the gesture. The old value always
    // comes from the shadow, never from the plugin, and the shadow is kept in
    // sync by VstPlugin on every host write and state restore.
    frame->onBeginEdit = nullptr;
    frame->onPerformEdit = [this](Steinberg::Vst::ParamID paramID,
                                    Steinberg::Vst::ParamValue newVal,
                                    uint64_t gestureID) {
        float oldValue = plugin->swapShadowParameterValue(static_cast<int>(paramID),
                                                          static_cast<float>(newVal));
        if (std::getenv("DAW_VST_DEBUG")) {
            std::cout << "[vst-perform] node=" << id << " param=" << paramID
                      << " " << oldValue << " -> " << newVal
                      << " gesture=" << gestureID << std::endl;
        }
        onPluginParameterChange(static_cast<int>(paramID), oldValue, static_cast<float>(newVal),
                                gestureID);
    };
    // Parameter undo is Ardour's model; the state-blob diff only covers what
    // performEdit cannot report (preset loads, internal routing) and runs when
    // the plugin says its state changed, or when the editor closes.
    frame->onStatePoll = [this](bool force) { pollVstStateForUndo(force); };
    frame->onStateReset = [this]() {
        stateResetPending_ = true;
        if (mpeRack_) markRackDirty();
        pollVstStateForUndo(true);
    };
    frame->onGestureEnd = [this]() {
        if (mpeRack_) markRackDirty();
    };
    plugin->refreshParameterShadow();
    frame->activeGestureCount = 0;
    stateBaselineValid_ = false;
    vstStateBaselineDirty = true;
}

// ============================================================================
// State-diff undo
// ============================================================================
//
// Anything the user changes inside the plugin GUI that is not an exposed
// parameter edit (Vital's LFO-to-knob drags, mod-matrix routing, internal
// toggles that skip beginEdit, preset browsing) only shows up in the
// component/controller state blobs. While the editor is open we snapshot those
// blobs at an interval and turn any unexplained difference into an undoable
// VstStateChangeAction.

void VstNode::pollVstStateForUndo(bool force) {
    if (!plugin || !plugin->isValid() || restoringState) return;
    if (!project || !project->um) return;

    // Mid-gesture (knob drag): the blob is churning with the dragged value,
    // which the param action already records. endEdit forces a poll when done.
    auto* frame = plugin->getHostFrame();
    if (frame && frame->activeGestureCount > 0) return;

    uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (!force && now - lastStatePollMs_ < 500) return;
    lastStatePollMs_ = now;

    // A mapped, driven parameter rewrites plugin state continuously, and a
    // byte diff can't separate its movement from user edits. Only when one has
    // actually moved since the baseline snapshot, re-baseline silently; idle
    // mappings don't block state undo.
    bool mappedMoved = false;
    for (auto& [pid, conn] : mappedVstParams) {
        if (!conn || !conn->is_connected) continue;
        auto it = mappedBaselineValues_.find(pid);
        if (it == mappedBaselineValues_.end() || it->second != plugin->getParameterValue(pid)) {
            mappedMoved = true;
            break;
        }
    }

    auto comp = plugin->getComponentState();
    auto ctrl = plugin->getControllerState();
    if (comp.empty() && ctrl.empty()) return;

    // A reset means the plugin changed its own state (preset). Diff even when
    // parameter edits have marked the baseline dirty, so the preset isn't
    // absorbed into a re-baseline.
    const bool stateReset = stateResetPending_;
    stateResetPending_ = false;

    if (mappedMoved || (vstStateBaselineDirty && !stateReset) || !stateBaselineValid_) {
        stateBaselineComp_ = std::move(comp);
        stateBaselineCtrl_ = std::move(ctrl);
        stateBaselineValid_ = true;
        vstStateBaselineDirty = false;
        mappedBaselineValues_.clear();
        for (auto& [pid, conn] : mappedVstParams)
            if (conn && conn->is_connected)
                mappedBaselineValues_[pid] = plugin->getParameterValue(pid);
        return;
    }

    if (comp == stateBaselineComp_ && ctrl == stateBaselineCtrl_) {
        vstStateBaselineDirty = false;
        return;
    }

    // No coalescing here: mutating an already-visible action would rewrite
    // history (redo would replay a later edit). Continuous drags don't spam
    // actions because polling waits for pointer-button release.
    json newState = json::object();
    newState["compState"] = jsonBytesEncode(comp);
    newState["ctrlState"] = jsonBytesEncode(ctrl);
    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};

    json oldState = json::object();
    oldState["compState"] = jsonBytesEncode(stateBaselineComp_);
    oldState["ctrlState"] = jsonBytesEncode(stateBaselineCtrl_);
    auto* pa = new VstStateChangeAction(project, std::move(mgrPath), static_cast<int>(id),
                                        std::move(oldState), std::move(newState), true);
    project->um->newAction(pa);
    stateBaselineComp_ = std::move(comp);
    stateBaselineCtrl_ = std::move(ctrl);
}

void VstNode::requestStatePollSoon() {
#ifndef __EMSCRIPTEN__
    if (plugin)
        if (auto* frame = plugin->getHostFrame())
            frame->statePollRequested = true;
#endif
}

// ============================================================================
// Serialization
// ============================================================================

json VstNode::extraSerialize() {
    json j;
    j["bypass"] = bypass.value;
    j["classIndex"] = classIndex_;
    if (!className_.empty()) j["className"] = className_;
    if (plugin && plugin->isValid()) {
        j["pluginPath"] = loadedPath;
        j["compState"] = jsonBytesEncode(plugin->getComponentState());
        j["ctrlState"] = jsonBytesEncode(plugin->getControllerState());
    }
    if (!mappedVstParams.empty()) {
        j["mappedVstParams"] = json::object();
        for (auto& [paramID, conn] : mappedVstParams)
            j["mappedVstParams"][std::to_string(paramID)] = conn->id;
    }
    return j;
}

void VstNode::extraDeSerialize(const json& j) {
    bypass.value = j.value("bypass", 0.0f);
    // Blobs are in sidecar files next to save.json; older saves had them inline as base64.
    auto readBlob = [this, &j](const char* fileKey, const char* inlineKey) -> std::vector<uint8_t> {
        if (j.contains(fileKey) && j[fileKey].is_string() && project) {
            std::filesystem::path folder(project->filepath);
            if (folder.is_relative())
                folder = std::filesystem::path(project->startupCWD) / folder;
            std::ifstream f(folder / j[fileKey].get<std::string>(), std::ios::binary);
            if (f.is_open())
                return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return {};
        }
        if (j.contains(inlineKey))
            return jsonBytesDecode(j[inlineKey]);
        return {};
    };
    if (j.contains("pluginPath")) {
        loadedPath = j["pluginPath"].get<std::string>();
        classIndex_ = j.value("classIndex", static_cast<size_t>(0));
        className_ = j.value("className", std::string());
        auto tPlugin = std::chrono::steady_clock::now();
        plugin = VstPluginCache::instance().getOrCreatePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{}, loadedPath, classIndex_);
        if (!plugin && classIndex_ != 0) {
            // The bundle changed and the saved class index no longer exists.
            classIndex_ = 0;
            plugin = VstPluginCache::instance().getOrCreatePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{}, loadedPath, classIndex_);
        }
        if (plugin) plugin->setUndoStatePoll(loadedPath.find("lsp-plugins") != std::string::npos);
        std::cout << "[load] vst node " << id << " (" << loadedPath << "): "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - tPlugin).count() << " ms" << std::endl;
        if (plugin && plugin->isValid()) {
            name = plugin->getName();
            className_ = plugin->getClassName();
            wirePluginCallbacks();
            // State is applied only after the plugin has been set up: some plugins
            // (LSP) fault if state I/O happens before setupProcessing.
            pendingCompState_.clear();
            pendingCtrlState_.clear();
            if (j.contains("compStateFile") || j.contains("compState"))
                pendingCompState_ = readBlob("compStateFile", "compState");
            if (j.contains("ctrlStateFile") || j.contains("ctrlState"))
                pendingCtrlState_ = readBlob("ctrlStateFile", "ctrlState");
            rebuildConnections();
            if (mpeRack_) createRackShadows();
            invalidateOwnerChain();
            int sr = sampleRate > 0 ? sampleRate : 48000;
            int bs = bufferSize > 0 ? bufferSize : 1024;
            plugin->setup(sr, bs);
            for (auto& shadow : rackShadows_)
                if (shadow) shadow->setup(sr, bs);
            applyPendingVstState();
            plugin->refreshParameterShadow();
            if (mpeRack_) markRackDirty();
            captureStateBaseline();
        } else {
            plugin = nullptr;
            rebuildConnections();
            invalidateOwnerChain();
        }
    } else {
        loadedPath.clear();
    }

    if (j.contains("mappedVstParams")) {
        for (auto& [key, val] : j["mappedVstParams"].items()) {
            int paramID = std::stoi(key);
            uint16_t connID = val;
            auto* c = new Connection;
            c->nm = inputs.nm;
            c->id = connID;
            c->type = DataType::Waveform;
            c->dir = Direction::input;
            c->is_connected = false;
            c->output_connection = c->id;
            c->output_node = inputs.nodeID;
            c->input_connection = -1;
            c->input_node = -1;
            c->events = nullptr;
            c->buffer = nullptr;
            c->bufferSize = 0;
            c->label = plugin ? plugin->getParameterNameByID(paramID) : ("VST Param " + std::to_string(paramID));
            inputs.connections.push_back(c);
            inputs.id_pool.reserveID(c->id);
            inputs.ids[c->id] = static_cast<uint16_t>(inputs.connections.size() - 1);
            mappedVstParams[paramID] = c;
        }
    }
}

// ============================================================================
// MPE channel allocator
// ============================================================================

int VstNode::allocMpeChannel(int noteId, HostEventList* plain,
                             std::array<HostEventList, 16>* rack) {
    auto it = noteChannels.find(noteId);
    if (it != noteChannels.end()) return it->second;
    bool used[16] = {};
    for (auto& [id, ch] : noteChannels) used[ch] = true;
    int ch = nextMpeChannel;
    bool found = false;
    for (int tries = 0; tries < 15; tries++) {
        if (ch == 0) ch = 1;
        if (!used[ch]) { found = true; break; }
        if (++ch > 15) ch = 1;
    }
    if (!found) {
        // All 15 MPE channels in use — evict the first entry and send note-off
        if (!noteChannels.empty()) {
            auto oldest = noteChannels.begin();
            ch = oldest->second;
            int evictedId = oldest->first;
            noteChannels.erase(oldest);
            evictedNoteChannels[evictedId] = ch;

            // Send note-off for the evicted note so it doesn't ring forever
            HostEventList& out = rack ? (*rack)[static_cast<size_t>(ch)] : *plain;
            Steinberg::Vst::Event off{};
            off.busIndex = 0;
            off.sampleOffset = 0;
            off.flags = 0;
            off.type = Steinberg::Vst::Event::kNoteOffEvent;
            off.noteOff.channel = static_cast<Steinberg::int16>(ch);
            off.noteOff.pitch = 0;
            off.noteOff.tuning = 0;
            off.noteOff.velocity = 0.0f;
            off.noteOff.noteId = evictedId;
            out.addEvent(off);
        } else {
            ch = 1;
        }
    }
    nextMpeChannel = (ch % 15) + 1;
    noteChannels[noteId] = ch;
    return ch;
}

void VstNode::freeMpeChannel(int noteId) {
    noteChannels.erase(noteId);
}

void VstNode::sendMpePitchBendRange(void* eventList) {
    if (mpeRangeSet) return;
    mpeRangeSet = true;
    auto& out = *static_cast<HostEventList*>(eventList);

    auto rpn = [&](int ch, int cc, int val) {
        Steinberg::Vst::Event ev{};
        ev.busIndex = 0;
        ev.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
        ev.midiCCOut.controlNumber = static_cast<Steinberg::uint8>(cc);
        ev.midiCCOut.channel = static_cast<Steinberg::int8>(ch);
        ev.midiCCOut.value = static_cast<Steinberg::int8>(val);
        ev.midiCCOut.value2 = 0;
        out.addEvent(ev);
    };

    // RPN 6 (MPE Configuration): lower zone, master ch 0, 15 member channels
    rpn(0, 101, 0); rpn(0, 100, 6);
    rpn(0, 6, 15);  rpn(0, 38, 0);
    rpn(0, 101, 127); rpn(0, 100, 127);

    // RPN 0 (Pitch Bend Sensitivity) = ±48 semitones (MPE default). Bends are
    // scaled to this range, so plugins using the MPE default are correct.
    for (int ch = 0; ch <= 15; ++ch) {
        rpn(ch, 101, 0); rpn(ch, 100, 0);
        rpn(ch, 6, 48);  rpn(ch, 38, 0);
        rpn(ch, 101, 127); rpn(ch, 100, 127);
    }
}

// ============================================================================
// VST parameter mapping
// ============================================================================

void VstNode::mapVstParameter(int paramID) {
    if (!project || !project->um) return;
    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new MapParameterUndoAction(project, std::move(mgrPath), static_cast<int>(id), 0, paramID);
    project->um->newAction(pa);
}

void VstNode::unmapVstParameter(int paramID) {
    if (!project || !project->um) return;
    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new UnmapParameterUndoAction(project, std::move(mgrPath), static_cast<int>(id), 0, paramID);
    project->um->newAction(pa);
}

bool VstNode::mapVstParameterNow(int paramID) {
    if (!nm || mappedVstParams.count(paramID)) return false;

    auto* c = new Connection;
    c->type = DataType::Waveform;
    c->dir = Direction::input;
    inputs.addConnection(c);
    c->label = plugin ? plugin->getParameterNameByID(paramID) : ("VST Param " + std::to_string(paramID));

    mappedVstParams[paramID] = c;
    makeConnectionRects();
    nm->markTopologyDirty();
    return true;
}

bool VstNode::unmapVstParameterNow(int paramID) {
    auto it = mappedVstParams.find(paramID);
    if (it == mappedVstParams.end()) return false;

    Connection* mc = it->second;
    if (mc->is_connected)
        nm->severConnectionNow(static_cast<uint16_t>(mc->input_node),
                               static_cast<uint16_t>(mc->input_connection),
                               id, mc->id);

    auto cit = std::find(inputs.connections.begin(), inputs.connections.end(), mc);
    if (cit != inputs.connections.end()) {
        inputs.id_pool.releaseID(mc->id);
        inputs.ids.erase(mc->id);
        inputs.connections.erase(cit);
    }
    delete mc;
    mappedVstParams.erase(it);

    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);

    makeConnectionRects();
    nm->markTopologyDirty();
    return true;
}

std::shared_ptr<TreeEntry> VstNode::getNodeMenu() {
    auto t = uTreeEntry();
    t->label = "Node Menu";

    if (this->id) {
        auto remove = uTreeEntry();
        remove->label = "Remove Node";
        remove->click = [this] () { nm->removeNode(this); };
        t->addChild(remove);
    }

    auto mapParam = uTreeEntry();
    mapParam->label = "Map Parameter";
    mapParam->maxListHeight = 300.f;

    // Regular node params (bypass)
    for (size_t pi = 0; pi < params.size(); ++pi) {
        auto* p = params[pi];
        if (!p) continue;
        std::string label;
        if (auto* k = dynamic_cast<Knob*>(p))
            label = k->label.empty() ? ("Param " + std::to_string(pi)) : k->label;
        else
            label = p->label.empty() ? ("Param " + std::to_string(pi)) : p->label;
        auto item = uTreeEntry();
        bool mapped = (p->mappedConnection != nullptr);
        item->label = (mapped ? "[M] " : "") + label;
        if (mapped)
            item->click = [this, pi]() { unmapParameter(pi); };
        else
            item->click = [this, pi]() { mapParameter(pi); };
        mapParam->addChild(item);
    }

    // VST plugin params
    if (plugin && plugin->isValid()) {
        int count = plugin->getParameterCount();
        for (int i = 0; i < count; ++i) {
            // Map by the parameter's stable ParamID, not its list index — the
            // ID is what queueParameterChange/setParamNormalized expect.
            int pid = plugin->getParameterID(i);
            if (pid == -1) continue;
            auto item = uTreeEntry();
            bool mapped = (mappedVstParams.count(pid) > 0);
            item->label = (mapped ? "[M] " : "") + plugin->getParameterName(i);
            if (mapped)
                item->click = [this, pid]() { unmapVstParameter(pid); };
            else
                item->click = [this, pid]() { mapVstParameter(pid); };
            mapParam->addChild(item);
        }
    }

    if (mapParam->children.size() > 0)
        t->addChild(mapParam);

    return t;
}
