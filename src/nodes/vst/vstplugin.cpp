#ifndef __EMSCRIPTEN__

#include "vstplugin.h"
#include "public.sdk/source/common/memorystream.h"
#include <dlfcn.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <poll.h>
#include <SDL3/SDL_events.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

// Prevent X11 errors from killing the app.
static int xErrorHandler(Display* d, XErrorEvent* e) {
    char buf[256];
    XGetErrorText(d, e->error_code, buf, sizeof(buf));
    std::cerr << "[X11 error] " << buf << " (opcode=" << (int)e->request_code
              << " resource=" << e->resourceid << ")" << std::endl;
    return 0;
}
static bool xErrorHandlerInstalled = []() {
    XSetErrorHandler(xErrorHandler);
    return true;
}();

// ============================================================================
// PlugLib
// ============================================================================

PlugLib::PlugLib(std::string path) : path(std::move(path)) {
    handle = dlopen(this->path.c_str(), RTLD_NOW);
    if (!handle) {
        std::cerr << "[PlugLib] dlopen failed: " << dlerror() << std::endl;
        return;
    }

    is_yabridge = dlsym(handle, "yabridge_version") != nullptr;

    // Call ModuleEntry before GetPluginFactory — required by VST3 spec,
    // and essential for yabridge chainloaders.
    using ModuleEntryProc = bool (*)();
    auto moduleEntry = reinterpret_cast<ModuleEntryProc>(dlsym(handle, "ModuleEntry"));
    if (moduleEntry) {
        moduleEntry();
    }

    using GetFactoryProc = Steinberg::FUnknown* (*)();
    auto getFactory = reinterpret_cast<GetFactoryProc>(dlsym(handle, "GetPluginFactory"));
    if (!getFactory) {
        std::cerr << "[PlugLib] dlsym GetPluginFactory failed: " << dlerror() << std::endl;
        dlclose(handle);
        handle = nullptr;
        return;
    }

    Steinberg::FUnknown* raw = getFactory();
    if (!raw) {
        std::cerr << "[PlugLib] GetPluginFactory returned null" << std::endl;
        dlclose(handle);
        handle = nullptr;
        return;
    }

    factory = Steinberg::FUnknownPtr<Steinberg::IPluginFactory>(raw);
    if (!factory) {
        std::cerr << "[PlugLib] Factory cast to IPluginFactory failed" << std::endl;
        dlclose(handle);
        handle = nullptr;
        return;
    }

    factoryWrapper = std::make_unique<VST3::Hosting::PluginFactory>(factory);
    loaded = fetchClassIDs();
    if (!loaded) {
        dlclose(handle);
        handle = nullptr;
    }
}

PlugLib::~PlugLib() {
    plugProvider.reset();
    factoryWrapper.reset();
    factory = nullptr;
    if (handle) {
        // Some plugins crash on dlclose, so we leak intentionally if loaded successfully.
        // dlclose(handle);
    }
}

bool PlugLib::fetchClassIDs() {
    bool foundComponent = false, foundController = false;

    for (Steinberg::int32 i = 0; i < factory->countClasses(); ++i) {
        Steinberg::PClassInfo info{};
        if (factory->getClassInfo(i, &info) != Steinberg::kResultTrue)
            continue;

        if (!foundComponent && std::strcmp(info.category, "Audio Module Class") == 0) {
            std::memcpy(componentCID, info.cid, sizeof(Steinberg::TUID));
            displayName = info.name;
            // Don't use PlugProvider here — it calls initialize(nullptr) which
            // yabridge-wrapped plugins reject. We create the component manually
            // in createComponent() where we can pass a proper context.
            foundComponent = true;
        }

        if (!foundController && std::strcmp(info.category, "Component Controller Class") == 0) {
            std::memcpy(controllerCID, info.cid, sizeof(Steinberg::TUID));
            foundController = true;
        }

        if (foundComponent && foundController) break;
    }

    if (!foundComponent || !foundController) {
        std::cerr << "[PlugLib] Failed to find component/controller CIDs" << std::endl;
        return false;
    }
    return true;
}

// ============================================================================
// VstPluginCache
// ============================================================================

VstPluginCache& VstPluginCache::instance() {
    static VstPluginCache cache;
    return cache;
}

PlugLib* VstPluginCache::load(const std::string& path) {
    auto it = loaded.find(path);
    if (it != loaded.end()) {
        return it->second.get();
    }
    auto plugLib = std::make_unique<PlugLib>(path);
    if (!plugLib->isLoaded()) {
        return nullptr;
    }
    PlugLib* ptr = plugLib.get();
    loaded.emplace(path, std::move(plugLib));
    return ptr;
}

void VstPluginCache::unload(const std::string& path) {
    loaded.erase(path);
}

bool VstPluginCache::isLoaded(const std::string& path) const {
    return loaded.find(path) != loaded.end();
}

// ============================================================================
// EditorHostFrame
// ============================================================================

Steinberg::tresult PLUGIN_API EditorHostFrame::queryInterface(const Steinberg::TUID _iid, void** obj) {
    if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::IPlugFrame::iid) ||
        Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::FUnknown::iid)) {
        *obj = static_cast<Steinberg::IPlugFrame*>(this);
        addRef();
        return Steinberg::kResultTrue;
    }
    if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IComponentHandler::iid)) {
        *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
        addRef();
        return Steinberg::kResultTrue;
    }
    if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Linux::IRunLoop::iid)) {
        *obj = static_cast<Steinberg::Linux::IRunLoop*>(this);
        addRef();
        return Steinberg::kResultTrue;
    }
    *obj = nullptr;
    return Steinberg::kNoInterface;
}

Steinberg::uint32 PLUGIN_API EditorHostFrame::addRef() {
    refCount++;
    return refCount;
}

Steinberg::uint32 PLUGIN_API EditorHostFrame::release() {
    if (refCount > 0) refCount--;
    return refCount;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::resizeView(Steinberg::IPlugView*, Steinberg::ViewRect* newSize) {
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::beginEdit(Steinberg::Vst::ParamID id) {
    if (onBeginEdit) onBeginEdit(id);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized) {
    if (onPerformEdit) {
        // Look up pre-edit value captured by VstNode in beginEdit callback
        auto it = preEditValues.find(id);
        Steinberg::Vst::ParamValue oldValue = (it != preEditValues.end()) ? it->second : valueNormalized;
        onPerformEdit(id, oldValue, valueNormalized);
        preEditValues.erase(id);
    }
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::endEdit(Steinberg::Vst::ParamID id) {
    preEditValues.erase(id);
    return Steinberg::kResultTrue;
}

void EditorHostFrame::capturePreEditValue(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue val) {
    preEditValues[id] = val;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::restartComponent(Steinberg::int32 flags) {
    return Steinberg::kResultTrue;
}

// --- IRunLoop ---

Steinberg::tresult PLUGIN_API EditorHostFrame::registerEventHandler(
    Steinberg::Linux::IEventHandler* handler, Steinberg::Linux::FileDescriptor fd)
{
    std::cout << "[RunLoop] registerEventHandler fd=" << fd << std::endl;
    fdHandlers.push_back({handler, fd});
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::unregisterEventHandler(
    Steinberg::Linux::IEventHandler* handler)
{
    fdHandlers.erase(std::remove_if(fdHandlers.begin(), fdHandlers.end(),
        [handler](const FdEntry& e) { return e.handler == handler; }),
        fdHandlers.end());
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::registerTimer(
    Steinberg::Linux::ITimerHandler* handler, Steinberg::Linux::TimerInterval milliseconds)
{
    std::cout << "[RunLoop] registerTimer interval=" << milliseconds << "ms" << std::endl;
    uint64_t now = getNowMs();
    timers.push_back({handler, milliseconds, now + milliseconds});
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::unregisterTimer(
    Steinberg::Linux::ITimerHandler* handler)
{
    timers.erase(std::remove_if(timers.begin(), timers.end(),
        [handler](const TimerEntry& e) { return e.handler == handler; }),
        timers.end());
    return Steinberg::kResultTrue;
}

uint64_t EditorHostFrame::getNowMs() const {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void EditorHostFrame::pollRunLoop() {
    // Fire expired timers
    uint64_t now = getNowMs();
    for (auto& t : timers) {
        if (now >= t.nextFireMs) {
            t.handler->onTimer();
            t.nextFireMs = now + t.intervalMs;
        }
    }

    // Poll registered file descriptors (non-blocking)
    if (!fdHandlers.empty()) {
        std::vector<struct pollfd> pfds;
        pfds.reserve(fdHandlers.size());
        for (auto& entry : fdHandlers) {
            pfds.push_back({entry.fd, POLLIN, 0});
        }
        int ret = poll(pfds.data(), pfds.size(), 0); // 0 = non-blocking
        if (ret > 0) {
            for (size_t i = 0; i < pfds.size(); ++i) {
                if (pfds[i].revents & POLLIN) {
                    fdHandlers[i].handler->onFDIsSet(fdHandlers[i].fd);
                }
            }
        }
    }
}

// ============================================================================
// LinuxEditorWindowHost
// ============================================================================

class LinuxEditorWindowHost : public EditorWindowHost {
public:
    LinuxEditorWindowHost() {
        display = XOpenDisplay(nullptr);
        if (!display) {
            std::cerr << "[VST] X11: Failed to open display" << std::endl;
            return;
        }

        int screen = DefaultScreen(display);
        window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                     100, 100, 600, 400, 1,
                                     BlackPixel(display, screen),
                                     WhitePixel(display, screen));

        XStoreName(display, window, "VST3 Editor");
        XSelectInput(display, window, ExposureMask | StructureNotifyMask);
        XMapWindow(display, window);
        XFlush(display);
        windowOpen = true;

        wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &wmDeleteMessage, 1);

        // Grab global shortcuts before they reach the plugin
        grabKey(XK_z, ControlMask);       // Ctrl+Z undo
        grabKey(XK_z, ControlMask | ShiftMask); // Ctrl+Shift+Z redo
        grabKey(XK_s, ControlMask);       // Ctrl+S save
        grabKey(XK_space, 0);             // Space play/pause
    }

    ~LinuxEditorWindowHost() override {
        if (display && window) {
            XUngrabKey(display, AnyKey, AnyModifier, window);
            XDestroyWindow(display, window);
            window = 0;
        }
        if (display) {
            XCloseDisplay(display);
            display = nullptr;
        }
    }

    void grabKey(KeySym keysym, unsigned int modifiers) {
        if (!display) return;
        int kc = XKeysymToKeycode(display, keysym);
        XGrabKey(display, kc, modifiers, window, True, GrabModeAsync, GrabModeAsync);
        if (modifiers != AnyModifier) {
            // Also grab with lock keys (CapsLock, NumLock, ScrollLock)
            XGrabKey(display, kc, modifiers | LockMask, window, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(display, kc, modifiers | Mod2Mask, window, True, GrabModeAsync, GrabModeAsync); // NumLock
            XGrabKey(display, kc, modifiers | LockMask | Mod2Mask, window, True, GrabModeAsync, GrabModeAsync);
        }
    }

    void* getNativeWindowHandle() override {
        return reinterpret_cast<void*>(window);
    }

    const char* getPlatformType() override {
        return Steinberg::kPlatformTypeX11EmbedWindowID;
    }

    bool tick() override {
        if (!display || !windowOpen) return false;

        while (XPending(display) > 0) {
            XEvent e;
            XNextEvent(display, &e);
            if (e.type == ConfigureNotify) {
                Steinberg::ViewRect newRect;
                newRect.left = 0;
                newRect.top = 0;
                newRect.right = e.xconfigure.width;
                newRect.bottom = e.xconfigure.height;
                if (view) view->onSize(&newRect);
            }
            if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == wmDeleteMessage) {
                windowOpen = false;
                return false;
            }
            if (e.type == KeyPress && e.xkey.window == window) {
                handleGrabbedKey(e.xkey);
            }
        }
        return true;
    }

    void handleGrabbedKey(XKeyEvent& ev) {
        KeySym sym = XLookupKeysym(&ev, 0);
        bool ctrl = (ev.state & ControlMask) != 0;
        bool shift = (ev.state & ShiftMask) != 0;

        SDL_Event sdle{};
        sdle.type = SDL_EVENT_KEY_DOWN;
        sdle.key.windowID = 1; // will be handled by WindowHandler

        if (sym == XK_z && ctrl && !shift) {
            sdle.key.scancode = SDL_SCANCODE_Z;
            sdle.key.key = SDLK_Z;
            sdle.key.mod = SDL_KMOD_CTRL;
        } else if (sym == XK_z && ctrl && shift) {
            sdle.key.scancode = SDL_SCANCODE_Z;
            sdle.key.key = SDLK_Z;
            sdle.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
        } else if (sym == XK_s && ctrl) {
            sdle.key.scancode = SDL_SCANCODE_S;
            sdle.key.key = SDLK_S;
            sdle.key.mod = SDL_KMOD_CTRL;
        } else if (sym == XK_space) {
            sdle.key.scancode = SDL_SCANCODE_SPACE;
            sdle.key.key = SDLK_SPACE;
            sdle.key.mod = 0;
        } else {
            return;
        }

        int pushed = SDL_PushEvent(&sdle);
        std::cout << "[VST keygrab] pushed " << (pushed == 1 ? "ok" : "FAIL") << " sym=" << sym
                  << " ctrl=" << ctrl << " shift=" << shift << std::endl;
    }

    void resize(int w, int h) override {
        if (display && window) {
            XResizeWindow(display, window, w, h);
            XFlush(display);
        }
    }

    bool setName(const char* name) override {
        if (!display || !window) return false;
        XStoreName(display, window, name);
        XFlush(display);
        return true;
    }

private:
    Display* display = nullptr;
    ::Window window = 0;
    Atom wmDeleteMessage = 0;
};

std::unique_ptr<EditorWindowHost> EditorWindowHost::create() {
    return std::make_unique<LinuxEditorWindowHost>();
}

// ============================================================================
// VstPlugin
// ============================================================================

VstPlugin::VstPlugin(const std::string& pluginPath) : pluginPath(pluginPath) {
    lib = VstPluginCache::instance().load(pluginPath);
    if (!lib) {
        return;
    }

    if (!createComponent()) { lib = nullptr; return; }

    name = lib->getName();
    vendor = lib->getName();
    Steinberg::PFactoryInfo factoryInfo;
    if (lib->getFactory()->getFactoryInfo(&factoryInfo) == Steinberg::kResultTrue) {
        vendor = factoryInfo.vendor;
    }

    hostFrame = std::make_unique<EditorHostFrame>();
    is_yabridge_ = lib->is_yabridge;

    // Buses: fast, no controller needed. setActive: deferred to setup().
    if (!setupBuses()) return;
    needsSetup_ = true;

    if (is_yabridge_) {
        // Controller, view creation: yabridge IPC, defer to showEditor bg thread.
        valid = true;
        return;
    }

    // Native: do everything now (fast, in-process).
    if (!createController()) return;
    connectComponentController();
    if (editController) {
        // Sync component state to controller so getParamNormalized() returns
        // the actual parameter values (not all zeros).
        auto compState = getComponentState();
        if (!compState.empty()) setControllerState(compState);

        editController->setComponentHandler(hostFrame.get());
        view = editController->createView(Steinberg::Vst::ViewType::kEditor);
    }
    valid = true;
}

VstPlugin::~VstPlugin() {
    hideEditor();
    if (audioProcessor) {
        audioProcessor->setProcessing(false);
    }
    if (component) {
        component->setActive(false);
    }
    // Free channel pointer arrays
    for (auto& bus : inputBuses) {
        if (bus.channelBuffers32) { delete[] bus.channelBuffers32; bus.channelBuffers32 = nullptr; }
    }
    for (auto& bus : outputBuses) {
        if (bus.channelBuffers32) { delete[] bus.channelBuffers32; bus.channelBuffers32 = nullptr; }
    }
    inputBusData.clear();
    outputBusData.clear();
    inputBuses.clear();
    outputBuses.clear();
    if (view) {
        view->release();
        view = nullptr;
    }
    hostFrame.reset();
}

bool VstPlugin::createComponent() {
    Steinberg::FUnknown* componentUnknown = nullptr;
    auto result = lib->getFactory()->createInstance(
        lib->getComponentCID(), Steinberg::Vst::IComponent::iid, (void**)&componentUnknown);

    if (result != Steinberg::kResultTrue || !componentUnknown) {
        std::cerr << "[VstPlugin] Failed to create component" << std::endl;
        return false;
    }

    component = Steinberg::FUnknownPtr<Steinberg::Vst::IComponent>(componentUnknown);
    if (!component) return false;

    Steinberg::FUnknown* processorUnknown = nullptr;
    result = component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&processorUnknown);
    if (result != Steinberg::kResultTrue || !processorUnknown) {
        std::cerr << "[VstPlugin] Failed to get IAudioProcessor" << std::endl;
        return false;
    }
    audioProcessor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(processorUnknown);

    // Pass a minimal FUnknown as context — yabridge/Serum need non-null
    struct DummyContext : Steinberg::FUnknown {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
            if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
                addRef(); *obj = this; return Steinberg::kResultTrue;
            }
            *obj = nullptr; return Steinberg::kNoInterface;
        }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
    };
    DummyContext ctx;
    if (component->initialize(&ctx) != Steinberg::kResultTrue) {
        std::cerr << "[VstPlugin] Component initialize failed" << std::endl;
        return false;
    }

    return true;
}

bool VstPlugin::createController() {
    if (editController) return true; // already created
    Steinberg::FUnknown* controllerUnknown = nullptr;
    auto result = lib->getFactory()->createInstance(
        lib->getControllerCID(), Steinberg::Vst::IEditController::iid, (void**)&controllerUnknown);

    if (result != Steinberg::kResultTrue || !controllerUnknown) {
        std::cerr << "[VstPlugin] Failed to create edit controller" << std::endl;
        return false;
    }

    editController = Steinberg::FUnknownPtr<Steinberg::Vst::IEditController>(controllerUnknown);
    if (!editController) return false;

    // Same DummyContext trick for the controller
    struct DummyContext : Steinberg::FUnknown {
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
            if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
                addRef(); *obj = this; return Steinberg::kResultTrue;
            }
            *obj = nullptr; return Steinberg::kNoInterface;
        }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
    };
    DummyContext ctx;
    if (editController->initialize(&ctx) != Steinberg::kResultTrue) {
        std::cerr << "[VstPlugin] Edit controller initialize failed" << std::endl;
        return false;
    }

    return true;
}

void VstPlugin::connectComponentController() {
    componentConnection = Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(component);
    controllerConnection = Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(editController);

    if (componentConnection && controllerConnection) {
        componentConnection->connect(controllerConnection);
        controllerConnection->connect(componentConnection);
    }
}

bool VstPlugin::setupBuses() {
    auto* amComp = component.get();

    Steinberg::int32 numInputs = amComp->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    Steinberg::int32 numOutputs = amComp->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);

    std::vector<Steinberg::Vst::SpeakerArrangement> inputArrangements;
    std::vector<Steinberg::Vst::SpeakerArrangement> outputArrangements;

    for (Steinberg::int32 i = 0; i < numInputs; ++i) {
        Steinberg::Vst::SpeakerArrangement arr;
        if (audioProcessor->getBusArrangement(Steinberg::Vst::kInput, i, arr) != Steinberg::kResultTrue)
            arr = Steinberg::Vst::SpeakerArr::kEmpty;
        inputArrangements.push_back(arr);
    }

    for (Steinberg::int32 i = 0; i < numOutputs; ++i) {
        Steinberg::Vst::SpeakerArrangement arr;
        if (audioProcessor->getBusArrangement(Steinberg::Vst::kOutput, i, arr) != Steinberg::kResultTrue)
            arr = Steinberg::Vst::SpeakerArr::kEmpty;
        outputArrangements.push_back(arr);
    }

    audioProcessor->setBusArrangements(
        inputArrangements.data(), static_cast<Steinberg::int32>(inputArrangements.size()),
        outputArrangements.data(), static_cast<Steinberg::int32>(outputArrangements.size()));

    // Activate all audio buses
    for (Steinberg::int32 i = 0; i < numInputs; ++i)
        amComp->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, true);
    for (Steinberg::int32 i = 0; i < numOutputs; ++i)
        amComp->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, true);

    // Activate all event buses
    Steinberg::int32 numEventIn = amComp->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
    Steinberg::int32 numEventOut = amComp->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kOutput);
    for (Steinberg::int32 i = 0; i < numEventIn; ++i)
        amComp->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, i, true);
    for (Steinberg::int32 i = 0; i < numEventOut; ++i)
        amComp->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kOutput, i, true);

    // Allocate bus buffers — we resize in setup() since we need bufferSize
    for (Steinberg::int32 i = 0; i < numInputs; ++i) {
        Steinberg::Vst::BusInfo busInfo{};
        amComp->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, busInfo);
        Steinberg::Vst::AudioBusBuffers bufs{};
        bufs.numChannels = busInfo.channelCount;
        bufs.silenceFlags = 0;
        inputBuses.push_back(bufs);
    }

    for (Steinberg::int32 i = 0; i < numOutputs; ++i) {
        Steinberg::Vst::BusInfo busInfo{};
        amComp->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, i, busInfo);
        Steinberg::Vst::AudioBusBuffers bufs{};
        bufs.numChannels = busInfo.channelCount;
        bufs.silenceFlags = 0;
        outputBuses.push_back(bufs);
    }

    return true;
}

bool VstPlugin::activateComponent() {
    if (component->setActive(true) != Steinberg::kResultTrue) {
        std::cerr << "[VstPlugin] Failed to activate component" << std::endl;
        return false;
    }
    return true;
}

void VstPlugin::setup(int sampleRate, int bufferSize) {
    if (!valid || !audioProcessor) return;

    if (needsSetup_) {
        needsSetup_ = false;
        component->setActive(true);
    }

    processBufferSize = bufferSize;
    processSampleRate = sampleRate;

    Steinberg::Vst::ProcessSetup processSetup{};
    processSetup.sampleRate = sampleRate;
    processSetup.maxSamplesPerBlock = bufferSize;
    processSetup.processMode = Steinberg::Vst::kRealtime;
    processSetup.symbolicSampleSize = Steinberg::Vst::kSample32;

    if (audioProcessor->setupProcessing(processSetup) != Steinberg::kResultTrue) {
        std::cerr << "[VstPlugin] setupProcessing failed" << std::endl;
        return;
    }

    // Free old channel pointer arrays
    for (auto& bus : inputBuses) {
        if (bus.channelBuffers32) {
            delete[] bus.channelBuffers32;
            bus.channelBuffers32 = nullptr;
        }
    }
    for (auto& bus : outputBuses) {
        if (bus.channelBuffers32) {
            delete[] bus.channelBuffers32;
            bus.channelBuffers32 = nullptr;
        }
    }

    inputBusData.clear();
    outputBusData.clear();

    for (auto& bus : inputBuses) {
        inputBusData.emplace_back(bufferSize * bus.numChannels, 0.0f);
        bus.channelBuffers32 = new float*[bus.numChannels];
        for (int ch = 0; ch < bus.numChannels; ++ch) {
            bus.channelBuffers32[ch] = inputBusData.back().data() + ch * bufferSize;
        }
    }

    for (auto& bus : outputBuses) {
        outputBusData.emplace_back(bufferSize * bus.numChannels, 0.0f);
        bus.channelBuffers32 = new float*[bus.numChannels];
        for (int ch = 0; ch < bus.numChannels; ++ch) {
            bus.channelBuffers32[ch] = outputBusData.back().data() + ch * bufferSize;
        }
    }

    if (audioProcessor->setProcessing(true) != Steinberg::kResultTrue) {
        std::cerr << "[VstPlugin] setProcessing(true) failed" << std::endl;
        return;
    }

    processingActive = true;
}

void VstPlugin::processAudio(int bufferSize, Steinberg::Vst::IEventList* inputEvents) {
    if (!valid || !audioProcessor || !processingActive || bypassed) return;

    // Zero output bus data
    for (auto& data : outputBusData) {
        std::fill(data.begin(), data.end(), 0.0f);
    }

    Steinberg::Vst::ProcessData processData{};
    processData.processMode = Steinberg::Vst::kRealtime;
    processData.symbolicSampleSize = Steinberg::Vst::kSample32;
    processData.numSamples = bufferSize;
    processData.inputs = inputBuses.data();
    processData.numInputs = static_cast<Steinberg::int32>(inputBuses.size());
    processData.outputs = outputBuses.data();
    processData.numOutputs = static_cast<Steinberg::int32>(outputBuses.size());
    processData.inputEvents = inputEvents;

    // Deliver host-initiated parameter changes (undo/redo etc.) to the processor.
    paramChanges_.clear();
    {
        std::lock_guard<std::mutex> lock(pendingParamMutex_);
        for (const auto& pc : pendingParamChanges_) {
            Steinberg::int32 qi = 0;
            if (auto* q = paramChanges_.addParameterData(pc.first, qi)) {
                Steinberg::int32 pi = 0;
                q->addPoint(0, pc.second, pi);
            }
        }
        pendingParamChanges_.clear();
    }
    if (paramChanges_.getParameterCount() > 0)
        processData.inputParameterChanges = &paramChanges_;

    Steinberg::Vst::ProcessContext context{};
    context.state = Steinberg::Vst::ProcessContext::kPlaying
                  | Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;
    context.sampleRate = processSampleRate;
    processData.processContext = &context;

    audioProcessor->process(processData);
}

void VstPlugin::showEditor() {
    std::cout << "[VstPlugin::showEditor] called" << std::endl;
    if (editorOpen) {
        std::cout << "[VstPlugin::showEditor] editor already open" << std::endl;
        return;
    }

    if (is_yabridge_) {
        // Ardour pattern: yabridge IPC on bg thread. Create parent window on GUI.
        editorHost = EditorWindowHost::create();
        if (!editorHost) { std::cerr << "[showEditor] no host" << std::endl; return; }
        auto nativeHandle = editorHost->getNativeWindowHandle();
        auto platformType = editorHost->getPlatformType();
        editorOpen = true;
        registerEditor(this);
        auto* eh = editorHost.get();
        auto* frame = hostFrame.get();
        std::thread([this, eh, frame, nativeHandle, platformType]() {
            createController();
            connectComponentController();
            if (!view && editController) {
                auto compState = getComponentState();
                if (!compState.empty()) setControllerState(compState);

                editController->setComponentHandler(frame);
                view = editController->createView(Steinberg::Vst::ViewType::kEditor);
            }
            if (!view) return;
            view->setFrame(frame);
            std::cerr << "[showEditor] yabridge bg: attached..." << std::endl;
            view->attached(nativeHandle, platformType);
            std::cerr << "[showEditor] yabridge bg: done" << std::endl;
        }).detach();
        std::cout << "[VstPlugin::showEditor] yabridge spawned on bg" << std::endl;
        return;
    }

    // Native: view created in constructor, full X11 embedding on GUI thread
    if (!view && editController) {
        view = editController->createView(Steinberg::Vst::ViewType::kEditor);
    }
    if (!view) {
        std::cerr << "[VstPlugin::showEditor] no view" << std::endl;
        return;
    }
    editorHost = EditorWindowHost::create();
    if (!editorHost) { std::cerr << "[showEditor] no host" << std::endl; return; }
    editorHost->view = view;
    auto nativeHandle = editorHost->getNativeWindowHandle();
    auto platformType = editorHost->getPlatformType();
    if (view->isPlatformTypeSupported(platformType) != Steinberg::kResultTrue) {
        std::cerr << "[showEditor] platform not supported" << std::endl;
        return;
    }
    view->setFrame(hostFrame.get());
    auto result = view->attached(nativeHandle, platformType);
    if (result == Steinberg::kResultTrue) {
        editorHost->setName(name.c_str());
        Steinberg::ViewRect vr;
        view->getSize(&vr);
        editorHost->resize(vr.right - vr.left, vr.bottom - vr.top);
        view->onSize(&vr);
        editorOpen = true;
        registerEditor(this);
        std::cout << "[VstPlugin::showEditor] native opened" << std::endl;
    }
}

void VstPlugin::hideEditor() {
    if (!editorOpen) return;
    unregisterEditor(this);
    if (view) {
        view->removed();
        view->release();
        view = nullptr;
    }
    editorHost.reset();
    editorOpen = false;
}

bool VstPlugin::tickEditor() {
    if (!editorOpen || !editorHost) return false;

    // Poll runloop — fires plugin timers and FD events
    if (hostFrame) hostFrame->pollRunLoop();

    if (!editorHost->tick()) {
        if (view) view->removed();
        editorHost.reset();
        editorOpen = false;
        return false;
    }
    return true;
}

// Per-node instance cache — a single VstPlugin shared across GUI + audio copies.
std::shared_ptr<VstPlugin> VstPluginCache::getOrCreatePlugin(int nodeID, const std::vector<int>& managerPath, const std::string& path) {
    CacheKey key{nodeID, managerPath};
    auto it = instances.find(key);
    if (it != instances.end()) {
        if (it->second->getPluginPath() == path)
            return it->second;
        instances.erase(it);
    }
    if (path.empty()) return nullptr;
    auto p = std::make_shared<VstPlugin>(path);
    if (!p->isValid()) return nullptr;
    instances[key] = p;
    return p;
}

void VstPluginCache::removePlugin(int nodeID, const std::vector<int>& managerPath) {
    instances.erase({nodeID, managerPath});
}

// Static editor tick — call from main event loop
static std::vector<VstPlugin*> openEditors;

void VstPlugin::registerEditor(VstPlugin* p) {
    openEditors.push_back(p);
}

void VstPlugin::unregisterEditor(VstPlugin* p) {
    openEditors.erase(std::remove(openEditors.begin(), openEditors.end(), p), openEditors.end());
}

void VstPlugin::tickAllEditors() {
    for (auto* p : openEditors) {
        if (p->editorOpen) p->tickEditor();
    }
}

int VstPlugin::getNumAudioInputs() const {
    if (!component) return 0;
    return component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
}

int VstPlugin::getNumAudioOutputs() const {
    if (!component) return 0;
    return component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
}

int VstPlugin::getAudioInputChannels(int busIndex) const {
    if (!component) return 0;
    Steinberg::Vst::BusInfo info{};
    if (component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, busIndex, info) == Steinberg::kResultTrue)
        return info.channelCount;
    return 0;
}

int VstPlugin::getAudioOutputChannels(int busIndex) const {
    if (!component) return 0;
    Steinberg::Vst::BusInfo info{};
    if (component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, busIndex, info) == Steinberg::kResultTrue)
        return info.channelCount;
    return 0;
}

int VstPlugin::getNumEventInputs() const {
    if (!component) return 0;
    return component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kInput);
}

int VstPlugin::getNumEventOutputs() const {
    if (!component) return 0;
    return component->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kOutput);
}

int VstPlugin::getParameterCount() const {
    if (!editController) return 0;
    return editController->getParameterCount();
}

float VstPlugin::getParameterValue(int paramID) const {
    if (!editController) return 0.0f;
    return static_cast<float>(editController->getParamNormalized(static_cast<Steinberg::Vst::ParamID>(paramID)));
}

void VstPlugin::setParameterValue(int paramID, float valueNormalized) {
    if (!editController || !hostFrame) return;
    editController->setParamNormalized(static_cast<Steinberg::Vst::ParamID>(paramID),
                                       static_cast<Steinberg::Vst::ParamValue>(valueNormalized));
    // The controller call above only updates the plugin GUI; the processor
    // hears the change via inputParameterChanges on the next audio block.
    queueParameterChange(static_cast<Steinberg::Vst::ParamID>(paramID),
                         static_cast<Steinberg::Vst::ParamValue>(valueNormalized));
}

void VstPlugin::queueParameterChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    std::lock_guard<std::mutex> lock(pendingParamMutex_);
    for (auto& pc : pendingParamChanges_) {
        if (pc.first == id) {
            pc.second = value;
            return;
        }
    }
    pendingParamChanges_.push_back({id, value});
}

std::vector<uint8_t> VstPlugin::getComponentState() const {
    if (!component) return {};
    Steinberg::MemoryStream stream;
    if (component->getState(&stream) == Steinberg::kResultTrue) {
        return {stream.getData(), stream.getData() + stream.getSize()};
    }
    return {};
}

std::vector<uint8_t> VstPlugin::getControllerState() const {
    if (!editController) return {};
    Steinberg::MemoryStream stream;
    if (editController->getState(&stream) == Steinberg::kResultTrue) {
        return {stream.getData(), stream.getData() + stream.getSize()};
    }
    return {};
}

void VstPlugin::setComponentState(const std::vector<uint8_t>& data) {
    if (!component || data.empty()) return;
    Steinberg::MemoryStream stream;
    Steinberg::int32 bytesWritten = 0;
    stream.write(const_cast<uint8_t*>(data.data()), static_cast<Steinberg::int32>(data.size()), &bytesWritten);
    Steinberg::int64 resultPos = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, &resultPos);
    component->setState(&stream);
}

void VstPlugin::setControllerState(const std::vector<uint8_t>& data) {
    if (!editController || data.empty()) return;
    Steinberg::MemoryStream stream;
    Steinberg::int32 bytesWritten = 0;
    stream.write(const_cast<uint8_t*>(data.data()), static_cast<Steinberg::int32>(data.size()), &bytesWritten);
    Steinberg::int64 resultPos = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, &resultPos);
    editController->setComponentState(&stream);
}

#endif // __EMSCRIPTEN__
