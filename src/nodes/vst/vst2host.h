#pragma once

#ifndef __EMSCRIPTEN__

// Minimal VST 2.4 host ABI. Only the structures and opcodes the DAW needs to
// load, process, automate and show a 2.x plugin. Kept separate from the VST3
// SDK headers so the two hosting paths never collide.

#include "pluginterfaces/vst/ivstevents.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;
typedef int16_t VstInt16;
typedef int64_t VstInt64;
typedef float Vst32Single;
typedef double Vst64Single;
typedef char VstInt8;

struct AEffect;

typedef VstIntPtr (*Vst2AudioMasterCallback)(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                             VstIntPtr value, void* ptr, float opt);

enum Vst2AEffectOpcodes {
    effOpen = 0,
    effClose,
    effSetProgram,
    effGetProgram,
    effSetProgramName,
    effGetProgramName,
    effGetParamLabel,
    effGetParamDisplay,
    effGetParamName,
    effGetVu,
    effSetSampleRate,
    effSetBlockSize,
    effMainsChanged,
    effEditGetRect,
    effEditOpen,
    effEditClose,
    effEditDraw,
    effEditMouse,
    effEditKey,
    effEditIdle,
    effEditTop,
    effEditSleep,
    effIdentify,
    effGetChunk,
    effSetChunk,
    effProcessEvents,
    effCanBeAutomated,
    effString2Parameter,
    effGetNumProgramCategories,
    effGetProgramNameIndexed,
    effCopyProgram,
    effConnectInput,
    effConnectOutput,
    effGetInputLatency,
    effGetOutputLatency,
    effGetPlugCategory,
    effGetCurrentPosition,
    effGetDestinationBuffer,
    effOfflineNotify,
    effOfflinePrepare,
    effOfflineRun,
    effProcessVarIo,
    effSetSpeakerArrangement,
    effSetBlockSizeAndSampleRate,
    effSetBypass,
    effGetEffectName,
    effGetErrorText,
    effGetVendorString,
    effGetProductString,
    effGetVendorVersion,
    effVendorSpecific,
    effCanDo,
    effGetTailSamples
};

enum Vst2AEffectFlags {
    effFlagsHasEditor = 1 << 0,
    effFlagsCanReplacing = 1 << 4,
    effFlagsIsSynth = 1 << 8,
    effFlagsCanDoubleReplacing = 1 << 12
};

enum Vst2AudioMasterOpcodes {
    audioMasterAutomate = 0,
    audioMasterVersion = 1,
    audioMasterCurrentId = 2,
    audioMasterIdle = 3,
    audioMasterPinConnected = 4,
    audioMasterWantMidi = 6,
    audioMasterGetTime = 7,
    audioMasterProcessEvents = 8,
    audioMasterSetTime = 9,
    audioMasterTempoAt = 10,
    audioMasterGetNumAutomatableParameters = 11,
    audioMasterGetParameterQuantization = 12,
    audioMasterIOChanged = 13,
    audioMasterNeedIdle = 14,
    audioMasterSizeWindow = 15,
    audioMasterGetSampleRate = 16,
    audioMasterGetBlockSize = 17,
    audioMasterGetInputLatency = 18,
    audioMasterGetOutputLatency = 19,
    audioMasterGetPreviousPlug = 20,
    audioMasterGetNextPlug = 21,
    audioMasterWillReplaceOrAccumulate = 22,
    audioMasterGetCurrentProcessLevel = 23,
    audioMasterGetAutomationState = 24,
    audioMasterOfflineStart = 25,
    audioMasterOfflineRead = 26,
    audioMasterOfflineWrite = 27,
    audioMasterOfflineGetCurrentPass = 28,
    audioMasterOfflineGetCurrentMetaPass = 29,
    audioMasterSetOutputSampleRate = 30,
    audioMasterGetOutputSpeakerArrangement = 31,
    audioMasterGetVendorString = 32,
    audioMasterGetProductString = 33,
    audioMasterGetVendorVersion = 34,
    audioMasterVendorSpecific = 35,
    audioMasterSetIcon = 36,
    audioMasterCanDo = 37,
    audioMasterGetLanguage = 38,
    audioMasterOpenWindow = 39,
    audioMasterCloseWindow = 40,
    audioMasterGetDirectory = 41,
    audioMasterUpdateDisplay = 42,
    audioMasterBeginEdit = 43,
    audioMasterEndEdit = 44,
    audioMasterOpenFileSelector = 45,
    audioMasterCloseFileSelector = 46,
    audioMasterEditFile = 47,
    audioMasterGetChunkFile = 48,
    audioMasterGetInputSpeakerArrangement = 49
};

enum Vst2ProcessLevel {
    kVstProcessLevelUnknown = 0,
    kVstProcessLevelUser = 1,
    kVstProcessLevelRealtime = 2,
    kVstProcessLevelPrefetch = 3,
    kVstProcessLevelOffline = 4
};

enum Vst2TimeInfoFlags {
    kVstTransportChanged = 1 << 0,
    kVstTransportPlaying = 1 << 1,
    kVstTransportCycleActive = 1 << 2,
    kVstNanosValid = 1 << 8,
    kVstPpqPosValid = 1 << 9,
    kVstTempoValid = 1 << 10,
    kVstBarsValid = 1 << 11,
    kVstCyclePosValid = 1 << 12,
    kVstTimeSigValid = 1 << 13,
    kVstSmpteValid = 1 << 14,
    kVstClockValid = 1 << 15
};

struct Vst2TimeInfo {
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    VstInt32 timeSigNumerator;
    VstInt32 timeSigDenominator;
    VstInt32 smpteOffset;
    VstInt32 smpteFrameRate;
    VstInt32 samplesToNextClock;
    VstInt32 flags;
};

struct Vst2Event {
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 data[4];
};

struct Vst2MidiEvent {
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 noteLength;
    VstInt32 noteOffset;
    VstInt8 midiData[4];
    VstInt8 detune;
    VstInt8 noteOffVelocity;
    VstInt8 reserved1;
    VstInt8 reserved2;
};

struct Vst2Events {
    VstInt32 numEvents;
    VstIntPtr reserved;
    Vst2Event* events[2];
};

enum {
    kVstMidiEventType = 1,
    kVstMidiEventIsRealtime = 1 << 0
};

struct ERect {
    VstInt16 top;
    VstInt16 left;
    VstInt16 bottom;
    VstInt16 right;
};

// The AEffect layout is fixed by the VST 2.4 ABI; plugins hand back a raw
// pointer to it, so field order/size must match exactly.
struct AEffect {
    VstInt32 magic;
    VstIntPtr (*dispatcher)(AEffect*, VstInt32, VstInt32, VstIntPtr, void*, float);
    void (*process)(AEffect*, float**, float**, VstInt32);
    void (*setParameter)(AEffect*, VstInt32, float);
    float (*getParameter)(AEffect*, VstInt32);

    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;

    VstInt32 flags;
    VstIntPtr resvd1;
    VstIntPtr resvd2;

    VstInt32 initialDelay;

    VstInt32 realQualities;
    VstInt32 offQualities;
    float ioRatio;

    void* object;
    void* user;

    VstInt32 uniqueID;
    VstInt32 version;

    void (*processReplacing)(AEffect*, float**, float**, VstInt32);
    void (*processDoubleReplacing)(AEffect*, double**, double**, VstInt32);

    char future[56];
};

constexpr VstInt32 kEffectMagic = 0x56737450; // 'VstP'

typedef AEffect* (*Vst2PluginMainProc)(Vst2AudioMasterCallback audioMaster);

// ---------------------------------------------------------------------------
// Vst2Plugin — one loaded VST 2.x instance behind the same surface VstPlugin
// exposes to VstNode. Yabridge's VST2 bridge is itself a normal 64-bit VST2,
// so 32-bit Windows plugins load through here unchanged.
// ---------------------------------------------------------------------------
class Vst2Plugin {
public:
    Vst2Plugin(const std::string& path, Vst2PluginMainProc mainFn);
    ~Vst2Plugin();

    bool isValid() const { return aeff != nullptr; }
    const std::string& getName() const { return pluginName; }
    const std::string& getVendor() const { return vendorName; }

    /** audio callback used by VstPlugin to report plugin-side parameter edits */
    std::function<void(int, float)> onAutomate;
    std::function<void(int)> onBeginEdit;
    std::function<void(int)> onEndEdit;
    /** The plugin changed its own program/state (audioMasterUpdateDisplay). */
    std::function<void()> onStateChanged;

    void setup(int sampleRate, int bufferSize);
    void process(int numSamples, float** inputs, int numInputs, float** outputs, int numOutputs,
                 Steinberg::Vst::IEventList* events);
    /** Update the process context the plugin sees through audioMasterGetTime. */
    void setProcessContext(double tempo, double ppq, double samplePos, bool playing);

    int numInputs() const { return aeff ? aeff->numInputs : 0; }
    int numOutputs() const { return aeff ? aeff->numOutputs : 0; }
    bool isSynth() const { return aeff && (aeff->flags & effFlagsIsSynth); }
    int latencySamples() const { return aeff ? aeff->initialDelay : 0; }

    int getParameterCount() const { return aeff ? aeff->numParams : 0; }
    float getParameterValue(int index) const;
    std::string getParameterName(int index) const;
    void setParameterValue(int index, float value);

    std::vector<uint8_t> getState() const;
    void setState(const std::vector<uint8_t>& data);
    void suspend();
    void resume();

    // Editor (X11 parent handle supplied by the host window)
    bool hasEditor() const { return aeff && (aeff->flags & effFlagsHasEditor); }
    bool getEditorRect(int& w, int& h) const;
    bool openEditor(void* parentHandle);
    void closeEditor();
    void editorIdle();
    bool editorIsOpen() const { return editorOpen; }

private:
    static VstIntPtr hostCallback(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                  VstIntPtr value, void* ptr, float opt);
    VstIntPtr handleCallback(AEffect* effect, VstInt32 opcode, VstInt32 index,
                             VstIntPtr value, void* ptr, float opt);
    VstIntPtr dispatch(VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt) const;

    bool buildEventBlock(Steinberg::Vst::IEventList* events, int numSamples);

    AEffect* aeff = nullptr;
    std::string path;
    std::string pluginName;
    std::string vendorName;

    int sampleRate = 48000;
    int blockSize = 256;
    bool processing = false;
    bool inStateRestore = false;
    bool stateWasProcessing = false;
    bool editorOpen = false;
    int editorW = 0;
    int editorH = 0;

    std::atomic<double> currentTempo{120.0};
    std::atomic<double> currentPpq{0.0};
    std::atomic<double> currentSamplePos{0.0};
    std::atomic<bool> currentPlaying{false};
    std::atomic<int> processLevel{kVstProcessLevelRealtime};

    // One block of VstEvents + VstMidiEvent payloads, reused across blocks.
    std::vector<uint8_t> eventBlock;
    Vst2Events* eventList = nullptr;
};

#endif // __EMSCRIPTEN__
