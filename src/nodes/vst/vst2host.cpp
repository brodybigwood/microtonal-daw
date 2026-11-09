#include "vst2host.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#ifndef __EMSCRIPTEN__

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

// The plugin may call the host callback while it is being constructed, before
// we can attach `this` to aeffect->user. Remember the instance for that window.
static thread_local Vst2Plugin* g_constructingVst2 = nullptr;
// A bridge may service the initial handshake on a helper thread, where the
// thread-local above is not visible.
static std::atomic<Vst2Plugin*> g_constructingVst2Any{nullptr};

// AEffect::user is not reliable: yabridge owns the AEffect it hands back and
// uses that field itself, so the value we write there is not what the callback
// later sees. Look instances up by the AEffect pointer instead, and fall back
// to the most recently created one while a bridge is still handshaking.
//
// Fixed lock-free slots: the audio thread calls this from inside process(), so
// it must never take a mutex. 64 covers an MPE rack (16 instances) plus a full
// graph of VST2 plugins. Registration is effect-first / plugin-second, lookup
// is plugin-first, and both sides only use acquire/release stores.
constexpr int kVst2MaxInstances = 64;
static std::atomic<AEffect*> g_vst2EffectSlots[kVst2MaxInstances];
static std::atomic<Vst2Plugin*> g_vst2PluginSlots[kVst2MaxInstances];
static std::atomic<Vst2Plugin*> g_lastVst2{nullptr};
// Non-null marker so two threads can never claim the same empty slot.
static Vst2Plugin* const kSlotClaiming = reinterpret_cast<Vst2Plugin*>(static_cast<std::uintptr_t>(1));

static void registerVst2Instance(AEffect* effect, Vst2Plugin* plugin) {
    for (int i = 0; i < kVst2MaxInstances; ++i) {
        Vst2Plugin* expected = nullptr;
        if (g_vst2PluginSlots[i].compare_exchange_strong(expected, kSlotClaiming,
                                                         std::memory_order_acq_rel)) {
            // Claimed the slot. effect is published before the lookup-visible
            // plugin pointer.
            g_vst2EffectSlots[i].store(effect, std::memory_order_relaxed);
            g_vst2PluginSlots[i].store(plugin, std::memory_order_release);
            g_lastVst2.store(plugin, std::memory_order_release);
            return;
        }
    }
    g_lastVst2.store(plugin, std::memory_order_release);
}

static void unregisterVst2Instance(AEffect* effect, Vst2Plugin* plugin) {
    for (int i = 0; i < kVst2MaxInstances; ++i) {
        if (g_vst2PluginSlots[i].load(std::memory_order_acquire) == plugin) {
            g_vst2PluginSlots[i].store(nullptr, std::memory_order_release);
            g_vst2EffectSlots[i].store(nullptr, std::memory_order_relaxed);
            break;
        }
    }
    Vst2Plugin* expected = plugin;
    g_lastVst2.compare_exchange_strong(expected, nullptr);
}

static Vst2Plugin* findVst2Instance(AEffect* effect) {
    if (!effect) return nullptr;
    for (int i = 0; i < kVst2MaxInstances; ++i) {
        Vst2Plugin* p = g_vst2PluginSlots[i].load(std::memory_order_acquire);
        if (!p || p == kSlotClaiming) continue;
        if (g_vst2EffectSlots[i].load(std::memory_order_relaxed) == effect) return p;
    }
    return nullptr;
}

static void copyHostString(void* ptr, const char* str) {
    if (ptr) std::strncpy(static_cast<char*>(ptr), str, 255);
}

VstIntPtr Vst2Plugin::hostCallback(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                   VstIntPtr value, void* ptr, float opt) {
    Vst2Plugin* self = g_constructingVst2;
    if (!self) self = g_constructingVst2Any.load();
    if (!self) self = findVst2Instance(effect);
    if (!self && effect) self = g_lastVst2.load();
    if (!self) return 0;
    return self->handleCallback(effect, opcode, index, value, ptr, opt);
}

VstIntPtr Vst2Plugin::handleCallback(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                     VstIntPtr value, void* ptr, float opt) {
    switch (opcode) {
    case audioMasterVersion:
        return 2400;
    case audioMasterAutomate:
        // Plugin-side parameter edit (knob drag / preset load).
        if (onAutomate) onAutomate(index, opt);
        return 0;
    case audioMasterBeginEdit:
        if (onBeginEdit) onBeginEdit(index);
        return 0;
    case audioMasterEndEdit:
        if (onEndEdit) onEndEdit(index);
        return 0;
    case audioMasterIdle:
        return 0;
    case audioMasterGetTime: {
        static thread_local Vst2TimeInfo info{};
        info = Vst2TimeInfo{};
        info.samplePos = currentSamplePos.load();
        info.sampleRate = static_cast<double>(sampleRate);
        info.ppqPos = currentPpq.load();
        info.tempo = currentTempo.load();
        info.timeSigNumerator = 4;
        info.timeSigDenominator = 4;
        info.smpteFrameRate = 24;
        info.flags = kVstPpqPosValid | kVstTempoValid | kVstTimeSigValid | kVstTransportChanged;
        if (currentPlaying.load()) info.flags |= kVstTransportPlaying;
        return reinterpret_cast<VstIntPtr>(&info);
    }
    case audioMasterProcessEvents:
        return 0;
    case audioMasterSetTime:
        return 0;
    case audioMasterTempoAt:
        return static_cast<VstIntPtr>(currentTempo.load() * 10000.0);
    case audioMasterGetNumAutomatableParameters:
        return aeff ? aeff->numParams : 0;
    case audioMasterGetParameterQuantization:
        return 0;
    case audioMasterIOChanged:
        return 1;
    case audioMasterNeedIdle:
        return 1;
    case audioMasterSizeWindow: {
        // Some plugins ask the host to resize the editor from inside the editor
        // thread. Record it; the GUI tick applies it to the X11 window.
        if (editorW > 0 && editorH > 0) { editorW = index; editorH = static_cast<int>(value); }
        return 1;
    }
    case audioMasterGetSampleRate:
        return sampleRate;
    case audioMasterGetBlockSize:
        return blockSize;
    case audioMasterGetInputLatency:
    case audioMasterGetOutputLatency:
        return 0;
    case audioMasterGetPreviousPlug:
    case audioMasterGetNextPlug:
        return 0;
    case audioMasterWillReplaceOrAccumulate:
        return 1;
    case audioMasterGetCurrentProcessLevel:
        return processLevel.load();
    case audioMasterGetAutomationState:
        return 1; // off
    case audioMasterSetOutputSampleRate:
        return 0;
    case audioMasterGetOutputSpeakerArrangement:
        return 0;
    case audioMasterGetVendorString:
        copyHostString(ptr, "Microtonal DAW");
        return 1;
    case audioMasterGetProductString:
        copyHostString(ptr, "Microtonal DAW");
        return 1;
    case audioMasterGetVendorVersion:
        return 1000;
    case audioMasterVendorSpecific:
        return 0;
    case audioMasterCanDo: {
        const char* can = static_cast<const char*>(ptr);
        if (!can) return 0;
        static const char* supported[] = {
            "send_vst_events", "receive_vst_events", "send_midi_event",
            "supply_idle", "get_time_info", "get_directory", "set_chunk",
            "bypass", "receive_vst_midi_event", "edit_draw"
        };
        for (const char* s : supported)
            if (std::strcmp(can, s) == 0) return 1;
        return 0;
    }
    case audioMasterGetLanguage:
        return 1; // kVstLangEnglish
    case audioMasterOpenWindow:
    case audioMasterCloseWindow:
        return 0;
    case audioMasterGetDirectory:
        return 0;
    case audioMasterUpdateDisplay:
        // The plugin's program/state changed (preset load, internal edit). This
        // is VST2's only notification of that; relay it to the host so the
        // patcher/rack can act immediately instead of polling.
        if (onStateChanged) onStateChanged();
        {
            static const bool dbg = std::getenv("DAW_VST_DEBUG") != nullptr;
            if (dbg) std::cout << "[Vst2] audioMasterUpdateDisplay " << pluginName << std::endl;
        }
        return 0;
    case audioMasterOpenFileSelector:
    case audioMasterCloseFileSelector:
        return 0;
    case audioMasterWantMidi:
        return 1;
    case audioMasterPinConnected:
        return 0;
    default:
        return 0;
    }
}

Vst2Plugin::Vst2Plugin(const std::string& p, Vst2PluginMainProc mainFn) : path(p) {
    if (!mainFn) return;

    g_constructingVst2 = this;
    g_constructingVst2Any.store(this);
    try {
        aeff = mainFn(hostCallback);
    } catch (const std::exception& e) {
        std::cerr << "[Vst2] " << path << " VSTPluginMain failed: " << e.what() << std::endl;
        aeff = nullptr;
    } catch (...) {
        aeff = nullptr;
    }
    g_constructingVst2 = nullptr;
    g_constructingVst2Any.store(nullptr);

    if (!aeff || aeff->magic != kEffectMagic) {
        std::cerr << "[Vst2] " << path << ": invalid AEffect (bad magic)" << std::endl;
        aeff = nullptr;
        return;
    }
    aeff->user = this;
    registerVst2Instance(aeff, this);

    char buf[256]{};
    if (dispatch(effGetEffectName, 0, 0, buf, 0) == 0 && buf[0] == 0) {
        // Bridge with a missing name: fall back to the file name.
        size_t slash = path.find_last_of('/');
        std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
        size_t dot = base.find_last_of('.');
        std::strncpy(buf, dot == std::string::npos ? base.c_str() : base.substr(0, dot).c_str(), 255);
    }
    pluginName = buf;

    char vbuf[256]{};
    dispatch(effGetVendorString, 0, 0, vbuf, 0);
    vendorName = vbuf;

    dispatch(effOpen, 0, 0, nullptr, 0);
    dispatch(effIdentify, 0, 0, nullptr, 0);

    std::cout << "[Vst2] loaded '" << pluginName << "' by '" << vendorName
              << "' params=" << aeff->numParams << " in=" << aeff->numInputs
              << " out=" << aeff->numOutputs
              << (isSynth() ? " (instrument)" : "") << std::endl;

    eventBlock.assign(sizeof(Vst2Events), 0);
    eventList = reinterpret_cast<Vst2Events*>(eventBlock.data());
    eventList->numEvents = 0;
    eventList->reserved = 0;
}

Vst2Plugin::~Vst2Plugin() {
    if (!aeff) return;
    if (editorOpen) closeEditor();
    if (processing) suspend();
    dispatch(effClose, 0, 0, nullptr, 0);
    unregisterVst2Instance(aeff, this);
    aeff = nullptr;
}

VstIntPtr Vst2Plugin::dispatch(VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt) const {
    if (!aeff || !aeff->dispatcher) return 0;
    // Bridges report a dead Wine host by throwing (yabridge: "read: Connection
    // reset by peer"). Letting that escape would terminate the whole DAW.
    try {
        return aeff->dispatcher(aeff, opcode, index, value, ptr, opt);
    } catch (const std::exception& e) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            std::cerr << "[Vst2] " << pluginName << " dispatcher failed: " << e.what() << std::endl;
    } catch (...) {
    }
    return 0;
}

void Vst2Plugin::setup(int sr, int bs) {
    if (!aeff) return;
    if (processing && sr == sampleRate && bs == blockSize) return;
    if (processing) suspend();
    sampleRate = sr;
    blockSize = bs;
    dispatch(effSetSampleRate, 0, 0, nullptr, static_cast<float>(sr));
    dispatch(effSetBlockSize, 0, bs, nullptr, 0);
    std::cout << "[Vst2] setup '" << pluginName << "' sr=" << sr << " bs=" << bs << std::endl;
}

void Vst2Plugin::resume() {
    if (!aeff || processing) return;
    dispatch(effMainsChanged, 0, 1, nullptr, 0);
    processing = true;
}

void Vst2Plugin::suspend() {
    if (!aeff || !processing) return;
    dispatch(effMainsChanged, 0, 0, nullptr, 0);
    processing = false;
}

void Vst2Plugin::setProcessContext(double tempo, double ppq, double samplePos, bool playing) {
    currentTempo.store(tempo);
    currentPpq.store(ppq);
    currentSamplePos.store(samplePos);
    currentPlaying.store(playing);
}

bool Vst2Plugin::buildEventBlock(Steinberg::Vst::IEventList* events, int numSamples) {
    const Steinberg::int32 count = events ? events->getEventCount() : 0;
    if (count <= 0) {
        eventList->numEvents = 0;
        return false;
    }

    const size_t n = static_cast<size_t>(count);
    const size_t pointersBytes = sizeof(Vst2Events) + (n > 2 ? (n - 2) * sizeof(Vst2Event*) : 0);
    eventBlock.assign(pointersBytes + n * sizeof(Vst2MidiEvent), 0);
    eventList = reinterpret_cast<Vst2Events*>(eventBlock.data());
    auto* midi = reinterpret_cast<Vst2MidiEvent*>(eventBlock.data() + pointersBytes);

    int written = 0;
    for (size_t i = 0; i < n; ++i) {
        Steinberg::Vst::Event ev{};
        if (events->getEvent(static_cast<Steinberg::int32>(i), ev) != Steinberg::kResultTrue) continue;

        Vst2MidiEvent& m = midi[written];
        m.type = kVstMidiEventType;
        m.byteSize = sizeof(Vst2MidiEvent);
        m.deltaFrames = std::clamp(static_cast<int>(ev.sampleOffset), 0, std::max(0, numSamples - 1));
        m.flags = kVstMidiEventIsRealtime;
        m.noteLength = 0;
        m.noteOffset = 0;
        m.detune = 0;
        m.noteOffVelocity = 0;
        m.reserved1 = 0;
        m.reserved2 = 0;

        bool ok = true;
        switch (ev.type) {
        case Steinberg::Vst::Event::kNoteOnEvent: {
            const int ch = std::clamp(static_cast<int>(ev.noteOn.channel), 0, 15);
            const int vel = std::clamp(static_cast<int>(ev.noteOn.velocity * 127.0f + 0.5f), 1, 127);
            m.midiData[0] = static_cast<VstInt8>(0x90 | ch);
            m.midiData[1] = static_cast<VstInt8>(std::clamp(static_cast<int>(ev.noteOn.pitch), 0, 127));
            m.midiData[2] = static_cast<VstInt8>(vel);
            m.midiData[3] = 0;
            break;
        }
        case Steinberg::Vst::Event::kNoteOffEvent: {
            const int ch = std::clamp(static_cast<int>(ev.noteOff.channel), 0, 15);
            const int vel = std::clamp(static_cast<int>(ev.noteOff.velocity * 127.0f + 0.5f), 0, 127);
            m.midiData[0] = static_cast<VstInt8>(0x80 | ch);
            m.midiData[1] = static_cast<VstInt8>(std::clamp(static_cast<int>(ev.noteOff.pitch), 0, 127));
            m.midiData[2] = static_cast<VstInt8>(vel);
            m.midiData[3] = 0;
            break;
        }
        case Steinberg::Vst::Event::kLegacyMIDICCOutEvent: {
            const int ch = std::clamp(static_cast<int>(ev.midiCCOut.channel), 0, 15);
            const int cc = ev.midiCCOut.controlNumber;
            const int v1 = static_cast<uint8_t>(ev.midiCCOut.value);
            const int v2 = static_cast<uint8_t>(ev.midiCCOut.value2);
            if (cc == Steinberg::Vst::kPitchBend) { // 129, NOT 128 (that is aftertouch)
                m.midiData[0] = static_cast<VstInt8>(0xE0 | ch);
                m.midiData[1] = static_cast<VstInt8>(std::clamp(v1, 0, 127));
                m.midiData[2] = static_cast<VstInt8>(std::clamp(v2, 0, 127));
                m.midiData[3] = 0;
            } else {
                m.midiData[0] = static_cast<VstInt8>(0xB0 | ch);
                m.midiData[1] = static_cast<VstInt8>(cc & 0x7F);
                m.midiData[2] = static_cast<VstInt8>(std::clamp(v1, 0, 127));
                m.midiData[3] = 0;
            }
            break;
        }
        case Steinberg::Vst::Event::kDataEvent:
        case Steinberg::Vst::Event::kPolyPressureEvent:
        case Steinberg::Vst::Event::kNoteExpressionValueEvent:
        case Steinberg::Vst::Event::kNoteExpressionTextEvent:
        case Steinberg::Vst::Event::kChordEvent:
        case Steinberg::Vst::Event::kScaleEvent:
        default:
            ok = false;
            break;
        }
        if (ok) {
            eventList->events[written] = reinterpret_cast<Vst2Event*>(&m);
            ++written;
        }
    }

    eventList->numEvents = written;
    return written > 0;
}

void Vst2Plugin::process(int numSamples, float** inputs, int numInputs, float** outputs, int numOutputs,
                         Steinberg::Vst::IEventList* events) {
    if (!aeff || !processing || numSamples <= 0) return;

    processLevel.store(kVstProcessLevelRealtime);
    if (events && buildEventBlock(events, numSamples)) {
        static const bool dbg = std::getenv("DAW_VST_DEBUG") != nullptr;
        if (dbg) {
            static std::atomic<int> calls{0};
            if (calls.fetch_add(1) % 64 == 0) {
                for (VstInt32 i = 0; i < eventList->numEvents; ++i) {
                    auto* m = reinterpret_cast<Vst2MidiEvent*>(eventList->events[i]);
                    const int st = static_cast<unsigned char>(m->midiData[0]);
                    if ((st & 0xF0) == 0x90 || (st & 0xF0) == 0xE0)
                        std::cout << "[vst2-midi] " << pluginName << " status=0x" << std::hex << st
                                  << std::dec << " d1=" << static_cast<int>(m->midiData[1])
                                  << " d2=" << static_cast<int>(m->midiData[2])
                                  << " delta=" << m->deltaFrames << std::endl;
                }
            }
        }
        const VstIntPtr r = dispatch(effProcessEvents, 0, 0, eventList, 0);
        if (dbg) {
            static std::atomic<int> rej{0};
            if (r != 0 && rej.fetch_add(1) < 5)
                std::cout << "[vst2-midi] effProcessEvents returned " << r << std::endl;
        }
    }

    try {
        if ((aeff->flags & effFlagsCanReplacing) && aeff->processReplacing) {
            aeff->processReplacing(aeff, inputs, outputs, numSamples);
        } else if (aeff->process) {
            // Accumulating process(): outputs must be silent before the call.
            for (int c = 0; c < numOutputs; ++c)
                if (outputs[c]) std::fill(outputs[c], outputs[c] + numSamples, 0.0f);
            aeff->process(aeff, inputs, outputs, numSamples);
        }
    } catch (const std::exception& e) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            std::cerr << "[Vst2] " << pluginName << " process failed: " << e.what() << std::endl;
    } catch (...) {
    }
}

float Vst2Plugin::getParameterValue(int index) const {
    if (!aeff || index < 0 || index >= aeff->numParams || !aeff->getParameter) return 0.0f;
    try {
        return aeff->getParameter(aeff, index);
    } catch (...) {
        return 0.0f;
    }
}

std::string Vst2Plugin::getParameterName(int index) const {
    if (!aeff || index < 0 || index >= aeff->numParams) return "Param " + std::to_string(index);
    char name[256]{};
    dispatch(effGetParamName, index, 0, name, 0);
    if (name[0] == 0) return "Param " + std::to_string(index);
    return name;
}

void Vst2Plugin::setParameterValue(int index, float value) {
    if (!aeff || index < 0 || index >= aeff->numParams || !aeff->setParameter) return;
    try {
        aeff->setParameter(aeff, index, std::clamp(value, 0.0f, 1.0f));
    } catch (...) {
    }
}

std::vector<uint8_t> Vst2Plugin::getState() const {
    if (!aeff) return {};
    void* chunk = nullptr;
    const VstIntPtr size = dispatch(effGetChunk, 0, 0, &chunk, 0);
    if (size <= 0 || !chunk) {
        // Never let a failed read pass as "no state": the save path would write
        // a 0-byte blob and the next load would reset the plugin to defaults.
        std::cerr << "[Vst2] getState failed for " << pluginName
                  << " (size=" << size << ", chunk=" << (chunk ? "ok" : "null") << ")" << std::endl;
        return {};
    }
    const auto* bytes = static_cast<const uint8_t*>(chunk);
    return {bytes, bytes + size};
}

void Vst2Plugin::setState(const std::vector<uint8_t>& data) {
    if (!aeff || data.empty()) return;
    dispatch(effSetChunk, 0, static_cast<VstIntPtr>(data.size()),
             const_cast<uint8_t*>(data.data()), 0);
}

bool Vst2Plugin::getEditorRect(int& w, int& h) const {
    w = 0;
    h = 0;
    if (!aeff || !hasEditor()) return false;
    ERect* rect = nullptr;
    dispatch(effEditGetRect, 0, 0, &rect, 0);
    if (!rect) return false;
    w = rect->right - rect->left;
    h = rect->bottom - rect->top;
    return w > 0 && h > 0;
}

bool Vst2Plugin::openEditor(void* parentHandle) {
    if (!aeff || !hasEditor() || editorOpen) return false;
    dispatch(effEditOpen, 0, 0, parentHandle, 0);
    editorOpen = true;
    getEditorRect(editorW, editorH);
    return true;
}

void Vst2Plugin::closeEditor() {
    if (!aeff || !editorOpen) return;
    dispatch(effEditClose, 0, 0, nullptr, 0);
    editorOpen = false;
}

void Vst2Plugin::editorIdle() {
    if (!aeff || !editorOpen) return;
    dispatch(effEditIdle, 0, 0, nullptr, 0);
}

#endif // __EMSCRIPTEN__
