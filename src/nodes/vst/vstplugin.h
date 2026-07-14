#pragma once

#ifndef __EMSCRIPTEN__

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/funknown.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <map>
#include <mutex>
#include <functional>
#include <atomic>

// ---------------------------------------------------------------------------
// PlugLib — caches a dlopen'd VST3 library and its factory state
// One per .vst3 file path, shared across all VstNode instances via VstPluginCache
// ---------------------------------------------------------------------------
class PlugLib {
public:
    explicit PlugLib(std::string path);
    ~PlugLib();

    bool isLoaded() const { return loaded; }

    Steinberg::IPluginFactory* getFactory() const { return factory; }
    VST3::Hosting::PluginFactory& getFactoryWrapper() { return *factoryWrapper; }
    Steinberg::Vst::PlugProvider& getPlugProvider() { return *plugProvider; }
    const Steinberg::TUID& getComponentCID() const { return componentCID; }
    const Steinberg::TUID& getControllerCID() const { return controllerCID; }

    const std::string& getPath() const { return path; }
    const std::string& getName() const { return displayName; }

private:
    void* handle = nullptr;
    bool loaded = false;
public:
    bool is_yabridge = false;

    Steinberg::FUnknownPtr<Steinberg::IPluginFactory> factory;
    std::unique_ptr<VST3::Hosting::PluginFactory> factoryWrapper;
    std::unique_ptr<Steinberg::Vst::PlugProvider> plugProvider;

    Steinberg::TUID componentCID{};
    Steinberg::TUID controllerCID{};

    std::string path;
    std::string displayName;

    bool fetchClassIDs();
};

class VstPlugin;

// ---------------------------------------------------------------------------
// HostEventList — simple IEventList backed by std::vector
// ---------------------------------------------------------------------------
#ifndef __EMSCRIPTEN__
class HostEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::int32 PLUGIN_API getEventCount() override { return static_cast<Steinberg::int32>(events.size()); }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index, Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(events.size())) return Steinberg::kInvalidArgument;
        e = events[index]; return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events.push_back(e); return Steinberg::kResultTrue;
    }
    void clear() { events.clear(); }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override { return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    std::vector<Steinberg::Vst::Event> events;
};

// ---------------------------------------------------------------------------
// HostParameterChanges — IParameterChanges fed into process() so host-initiated
// parameter edits (undo/redo, scripted actions) reach the audio processor, not
// just the edit controller. One value point per parameter at sample offset 0.
// ---------------------------------------------------------------------------
class HostParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    Steinberg::Vst::ParamID paramId = 0;
    Steinberg::Vst::ParamValue value = 0.0;

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset,
                                           Steinberg::Vst::ParamValue& v) override {
        if (index != 0) return Steinberg::kInvalidArgument;
        sampleOffset = 0;
        v = value;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32, Steinberg::Vst::ParamValue v,
                                           Steinberg::int32& index) override {
        value = v;
        index = 0;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override { return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

class HostParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::int32 PLUGIN_API getParameterCount() override { return static_cast<Steinberg::int32>(queues.size()); }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues.size())) return nullptr;
        return &queues[static_cast<size_t>(index)];
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id,
                                                                  Steinberg::int32& index) override {
        for (size_t i = 0; i < queues.size(); ++i) {
            if (queues[i].paramId == id) {
                index = static_cast<Steinberg::int32>(i);
                return &queues[i];
            }
        }
        queues.emplace_back();
        queues.back().paramId = id;
        index = static_cast<Steinberg::int32>(queues.size() - 1);
        return &queues.back();
    }
    void clear() { queues.clear(); }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override { return Steinberg::kNoInterface; }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    // deque: addParameterData hands out pointers that must stay valid while
    // more queues are appended during the same block.
    std::deque<HostParamValueQueue> queues;
};
#endif

// ---------------------------------------------------------------------------
// VstPluginCache — singleton that owns all loaded PlugLib instances
// ---------------------------------------------------------------------------
class VstPluginCache {
public:
    static VstPluginCache& instance();

    PlugLib* load(const std::string& path);   // returns cached or loads fresh
    void unload(const std::string& path);     // remove from cache
    bool isLoaded(const std::string& path) const;

    // Per-node VstPlugin instance cache (one shared across GUI+audio copies).
    std::shared_ptr<VstPlugin> getOrCreatePlugin(int nodeID, const std::vector<int>& managerPath, const std::string& path);
    void removePlugin(int nodeID, const std::vector<int>& managerPath);

private:
    std::unordered_map<std::string, std::unique_ptr<PlugLib>> loaded;

    using CacheKey = std::pair<int, std::vector<int>>;
    std::map<CacheKey, std::shared_ptr<VstPlugin>> instances;
};

// ---------------------------------------------------------------------------
// EditorWindowHost — abstract X11 window for the plugin editor
// ---------------------------------------------------------------------------
class EditorWindowHost {
public:
    virtual ~EditorWindowHost() = default;

    virtual void* getNativeWindowHandle() = 0;
    virtual const char* getPlatformType() = 0;
    virtual bool tick() = 0;
    virtual void resize(int w, int h) = 0;
    virtual bool setName(const char* name) = 0;
    /** True while any pointer button is held — state-diff polling waits for
        release so a continuous drag inside the plugin commits as one action. */
    virtual bool pointerButtonDown() { return false; }

    Steinberg::IPlugView* view = nullptr;
    bool windowOpen = false;

    static std::unique_ptr<EditorWindowHost> create();
};

// ---------------------------------------------------------------------------
// EditorHostFrame — bridges IPlugFrame and IComponentHandler for one plugin
// ---------------------------------------------------------------------------
class EditorHostFrame : public Steinberg::IPlugFrame,
                        public Steinberg::Vst::IComponentHandler,
                        public Steinberg::Linux::IRunLoop {
public:
    // Parameter edit callbacks
    std::function<void(Steinberg::Vst::ParamID)> onBeginEdit;
    std::function<void(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue, Steinberg::Vst::ParamValue)> onPerformEdit; // (paramID, oldValue, newValue)

    /** State-diff undo driver, invoked from tickEditor on the GUI thread only.
        `force` skips the poll interval (window closing, restartComponent). */
    std::function<void(bool)> onStatePoll;
    /** Set by restartComponent (any thread), consumed by the next tick. */
    bool statePollRequested = false;
    /** Open beginEdit/endEdit gestures; state-diff polling waits them out. */
    int activeGestureCount = 0;

    // Called by VstNode to snapshot pre-edit parameter value
    void capturePreEditValue(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue val);
    std::unordered_map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue> preEditValues;

    // IPlugFrame
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;

    // IComponentHandler
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;

    // IRunLoop
    Steinberg::tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler* handler,
                                                         Steinberg::Linux::FileDescriptor fd) override;
    Steinberg::tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler* handler) override;
    Steinberg::tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler* handler,
                                                  Steinberg::Linux::TimerInterval milliseconds) override;
    Steinberg::tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler* handler) override;

    // Called from tick() to fire pending events
    void pollRunLoop();

    // FUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;

private:
    Steinberg::uint32 refCount = 0;

    struct FdEntry {
        Steinberg::Linux::IEventHandler* handler;
        Steinberg::Linux::FileDescriptor fd;
    };
    struct TimerEntry {
        Steinberg::Linux::ITimerHandler* handler;
        uint64_t intervalMs;
        uint64_t nextFireMs;
    };
    std::vector<FdEntry> fdHandlers;
    std::vector<TimerEntry> timers;

    uint64_t getNowMs() const;
};

// ---------------------------------------------------------------------------
// VstPlugin — one loaded VST3 plugin instance (DSP + editor)
// ---------------------------------------------------------------------------
class VstPlugin {
public:
    VstPlugin(const std::string& pluginPath);
    ~VstPlugin();

    bool isValid() const { return valid; }

    const std::string& getName() const { return name; }
    const std::string& getVendor() const { return vendor; }
    const std::string& getPluginPath() const { return pluginPath; }

    void setup(int sampleRate, int bufferSize);
    void processAudio(int bufferSize, Steinberg::Vst::IEventList* inputEvents = nullptr,
                       double tempo = 120.0, double projectTimeMusic = 0.0, double projectTimeSamples = 0.0,
                       bool isPlaying = true);
    void setBypassed(bool b) { bypassed = b; }
    bool isBypassed() const { return bypassed; }

    // Bus buffer storage — VstNode reads/writes these for routing
    std::vector<std::vector<float>> inputBusData;
    std::vector<std::vector<float>> outputBusData;
#ifndef __EMSCRIPTEN__
    std::vector<Steinberg::Vst::AudioBusBuffers> inputBuses;
    std::vector<Steinberg::Vst::AudioBusBuffers> outputBuses;
#endif

    // Editor
    void showEditor();
    void hideEditor();
    bool isEditorOpen() const { return editorOpen; }
    bool tickEditor();

    // Global list of open editors — ticked from NodeEditor event loop.
    static void tickAllEditors();
    /** Force a state-diff capture on every open editor so pending plugin-internal
        edits become undo actions NOW — call before undo-tree navigation. */
    static void commitPendingStateEdits();
    static void registerEditor(VstPlugin* p);
    static void unregisterEditor(VstPlugin* p);
    int getEditorWidth() const { return editorW; }
    int getEditorHeight() const { return editorH; }

    // Bus configuration
    int getNumAudioInputs() const;
    int getNumAudioOutputs() const;
    int getAudioInputChannels(int busIndex) const;
    int getAudioOutputChannels(int busIndex) const;
    int getNumEventInputs() const;
    int getNumEventOutputs() const;

    // Parameter access. VST3 distinguishes a parameter's list position (index)
    // from its stable ParamID tag — they only coincide for some plugins.
    int getParameterCount() const;
    /** Real ParamID of the parameter at list position `paramIndex`, or -1. */
    int getParameterID(int paramIndex) const;
    float getParameterValue(int paramID) const;
    std::string getParameterName(int paramIndex) const;
    std::string getParameterNameByID(int paramID) const;
    /** Updates the edit controller AND queues the change for the processor
        (delivered via inputParameterChanges on the next audio block). */
    void setParameterValue(int paramID, float valueNormalized);
    void queueParameterChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

    // State serialization
    std::vector<uint8_t> getComponentState() const;
    std::vector<uint8_t> getControllerState() const;
    void setComponentState(const std::vector<uint8_t>& data);
    void setControllerState(const std::vector<uint8_t>& data);

    EditorHostFrame* getHostFrame() { return hostFrame.get(); }
    Steinberg::Vst::IEditController* getEditController() { return editController.get(); }

private:
    bool valid = false;
    bool needsSetup_ = false;
    bool is_yabridge_ = false;
    std::string pluginPath;
    std::string name;
    std::string vendor;
    std::atomic<bool> bypassed{false};

    PlugLib* lib = nullptr;

    // Steinberg interfaces
    Steinberg::FUnknownPtr<Steinberg::Vst::IComponent> component;
    Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> audioProcessor;
    Steinberg::FUnknownPtr<Steinberg::Vst::IEditController> editController;
    Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> componentConnection;
    Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> controllerConnection;

    std::unique_ptr<EditorHostFrame> hostFrame;

    // Editor
    Steinberg::IPlugView* view = nullptr;
    std::unique_ptr<EditorWindowHost> editorHost;
    bool editorOpen = false;
    int editorW = 0, editorH = 0;

    int processBufferSize = 0;
    int processSampleRate = 0;
    bool processingActive = false;

    // Host-initiated parameter changes pending delivery to the processor.
    std::mutex pendingParamMutex_;
    std::vector<std::pair<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue>> pendingParamChanges_;
    HostParameterChanges paramChanges_;   // reused each block, audio thread only

    bool createComponent();
    bool createController();
    void connectComponentController();
    bool setupBuses();
    bool activateComponent();
};

#else // __EMSCRIPTEN__

// Emscripten stub — no VST3 support
class VstPlugin {
public:
    VstPlugin(const std::string&) {}
    ~VstPlugin() = default;
    bool isValid() const { return false; }
    const std::string& getName() const { static std::string s = "VST"; return s; }
    const std::string& getVendor() const { static std::string s; return s; }
    const std::string& getPluginPath() const { static std::string s; return s; }
    void setup(int, int) {}
    void processAudio(int, void* = nullptr, double = 120.0, double = 0.0, double = 0.0, bool = true) {}
    void setBypassed(bool) {}
    bool isBypassed() const { return true; }
    void showEditor() {}
    void hideEditor() {}
    bool isEditorOpen() const { return false; }
    bool tickEditor() { return false; }
    static void commitPendingStateEdits() {}
    int getEditorWidth() const { return 0; }
    int getEditorHeight() const { return 0; }
    int getNumAudioInputs() const { return 0; }
    int getNumAudioOutputs() const { return 0; }
    int getAudioInputChannels(int) const { return 0; }
    int getAudioOutputChannels(int) const { return 0; }
    int getNumEventInputs() const { return 0; }
    int getNumEventOutputs() const { return 0; }
    int getParameterCount() const { return 0; }
    int getParameterID(int) const { return -1; }
    float getParameterValue(int) const { return 0; }
    std::string getParameterName(int) const { return {}; }
    std::string getParameterNameByID(int) const { return {}; }
    void setParameterValue(int, float) {}
    std::vector<uint8_t> getComponentState() const { return {}; }
    std::vector<uint8_t> getControllerState() const { return {}; }
    void setComponentState(const std::vector<uint8_t>&) {}
    void setControllerState(const std::vector<uint8_t>&) {}
    void* getHostFrame() { return nullptr; }
    std::vector<std::vector<float>> inputBusData;
    std::vector<std::vector<float>> outputBusData;
};

#endif // __EMSCRIPTEN__
