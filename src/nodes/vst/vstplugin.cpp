#ifndef __EMSCRIPTEN__

#include "vstplugin.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/keycodes.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include <cstdlib>
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

thread_local bool g_vstLoadingChainInsert = false;

// Must run before SDL/any other Xlib use: plugin editors and SDL touch Xlib
// from different threads, and without this Xlib's global state races (seen as
// XOpenDisplay hanging forever when opening a plugin editor).
void initVstX11Threads() { XInitThreads(); }

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
    struct Entry {
        std::string name;
        Steinberg::TUID cid{};
    };
    std::vector<Entry> components, controllers;

    for (Steinberg::int32 i = 0; i < factory->countClasses(); ++i) {
        Steinberg::PClassInfo info{};
        if (factory->getClassInfo(i, &info) != Steinberg::kResultTrue)
            continue;
        Entry e;
        e.name = info.name;
        std::memcpy(e.cid, info.cid, sizeof(Steinberg::TUID));
        if (std::strcmp(info.category, "Audio Module Class") == 0)
            components.push_back(std::move(e));
        else if (std::strcmp(info.category, "Component Controller Class") == 0)
            controllers.push_back(std::move(e));
    }

    if (components.empty()) {
        std::cerr << "[PlugLib] Failed to find any Audio Module Class" << std::endl;
        return false;
    }

    // Pair each component with its controller by class name; fall back to index order
    // when a factory names them differently.
    std::vector<bool> used(controllers.size(), false);
    classes.clear();
    classes.reserve(components.size());
    for (const auto& comp : components) {
        ClassInfo ci;
        ci.name = comp.name;
        std::memcpy(ci.componentCID, comp.cid, sizeof(Steinberg::TUID));
        for (size_t j = 0; j < controllers.size(); ++j) {
            if (!used[j] && controllers[j].name == comp.name) {
                std::memcpy(ci.controllerCID, controllers[j].cid, sizeof(Steinberg::TUID));
                ci.hasController = true;
                used[j] = true;
                break;
            }
        }
        classes.push_back(ci);
    }
    if (controllers.size() == components.size()) {
        for (size_t i = 0; i < classes.size(); ++i) {
            if (!classes[i].hasController) {
                std::memcpy(classes[i].controllerCID, controllers[i].cid, sizeof(Steinberg::TUID));
                classes[i].hasController = true;
            }
        }
    }

    // Legacy fields describe the first class (used where a class isn't specified).
    std::memcpy(componentCID, classes[0].componentCID, sizeof(Steinberg::TUID));
    std::memcpy(controllerCID, classes[0].controllerCID, sizeof(Steinberg::TUID));
    displayName = classes[0].name;
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

namespace {
// Minimal IHostApplication. A bare FUnknown context makes LSP (and others) crash on
// a null host pointer during initialize; plugins may call getName() unconditionally.
class HostApplicationContext : public Steinberg::Vst::IHostApplication {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IHostApplication::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            addRef();
            *obj = this;
            return Steinberg::kResultTrue;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return ++refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        if (--refCount == 0) return 0;
        return refCount;
    }

    Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override {
        const char* n = "Microtonal DAW";
        int i = 0;
        for (; i < 127 && n[i]; ++i) name[i] = static_cast<Steinberg::Vst::TChar>(n[i]);
        name[i] = 0;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID, Steinberg::TUID, void** obj) override {
        if (obj) *obj = nullptr;
        return Steinberg::kNotImplemented;
    }

private:
    std::atomic<Steinberg::uint32> refCount{1};
};

HostApplicationContext g_hostContext;

bool vstDebug() {
    static const bool on = std::getenv("DAW_VST_DEBUG") != nullptr;
    return on;
}
} // namespace

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
    if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::Vst::IComponentHandler2::iid)) {
        *obj = static_cast<Steinberg::Vst::IComponentHandler2*>(this);
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
    // The gesture hasn't changed any value yet — commit pending internal edits
    // now so the post-gesture re-baseline can't absorb them.
    if (activeGestureCount == 0 && onStatePoll) onStatePoll(true);
    activeGestureCount++;
    // Open a gesture for this parameter (Ardour: BeginGesture -> StartTouch).
    paramGestures[id] = nextGestureId++;
    if (onBeginEdit) onBeginEdit(id);
    return Steinberg::kResultTrue;
}

uint64_t EditorHostFrame::gestureFor(Steinberg::Vst::ParamID id) {
    const uint64_t now = getNowMs();
    static constexpr uint64_t kFreeGestureGapMs = 400;

    auto it = paramGestures.find(id);
    if (it != paramGestures.end()) {
        const uint64_t explicitId = it->second;
        if (!pointerIsDown) {
            // Wheel/key gesture without a held button: the plugin may bracket
            // every notch in its own beginEdit/endEdit (LSP EQ). Merge notches
            // of the same parameter that are closer than a pause.
            auto fg = freeGestures.find(id);
            if (fg != freeGestures.end() && now - freeLastUseMs[id] < kFreeGestureGapMs) {
                freeLastUseMs[id] = now;
                return fg->second;
            }
            freeGestures[id] = explicitId;
            freeLastUseMs[id] = now;
        }
        return explicitId;
    }

    // No beginEdit. Most such plugins still mean "one knob drag = one gesture",
    // so group by the editor's pointer button. A fresh press starts a new drag
    // session; a short tail after release keeps the final event in the same step.
    if (pointerIsDown) {
        auto ig = implicitGestures.find(id);
        if (ig == implicitGestures.end() || implicitGestureSession[id] != dragSession) {
            implicitGestures[id] = nextGestureId++;
            implicitGestureSession[id] = dragSession;
        }
        implicitLastUseMs[id] = now;
        return implicitGestures[id];
    }
    auto ig = implicitGestures.find(id);
    if (ig != implicitGestures.end() && now - implicitLastUseMs[id] < 250) {
        implicitLastUseMs[id] = now;
        return ig->second;
    }
    if (ig != implicitGestures.end()) implicitGestures.erase(ig);

    // Wheel ticks and key nudges have no button held and no beginEdit. Group
    // successive edits of the same parameter separated by less than a pause.
    auto fg = freeGestures.find(id);
    if (fg != freeGestures.end() && now - freeLastUseMs[id] < kFreeGestureGapMs) {
        freeLastUseMs[id] = now;
        return fg->second;
    }
    const uint64_t gid = nextGestureId++;
    freeGestures[id] = gid;
    freeLastUseMs[id] = now;
    return gid;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue valueNormalized) {
    uint64_t gesture = gestureFor(id);
    if (onPerformEdit) onPerformEdit(id, valueNormalized, gesture);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::endEdit(Steinberg::Vst::ParamID id) {
    if (activeGestureCount > 0) activeGestureCount--;
    // Gesture over — re-baseline (or diff) on the next tick instead of waiting
    // out the poll interval, so an internal edit made right after a knob drag
    // isn't absorbed into the post-drag baseline.
    statePollRequested = true;
    paramGestures.erase(id);
    if (activeGestureCount == 0 && onGestureEnd) onGestureEnd();
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::restartComponent(Steinberg::int32 flags) {
    // Plugin-initiated state reloads (preset change etc.) — poll for undo promptly.
    statePollRequested = true;
    stateResetRequested = true;
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::setDirty(Steinberg::TBool state) {
    // "something besides parameters changed" — preset load, internal routing.
    if (state) {
        statePollRequested = true;
        stateResetRequested = true;
    }
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::requestOpenEditor(Steinberg::FIDString) {
    return Steinberg::kNotImplemented;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::startGroupEdit() {
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::finishGroupEdit() {
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
        // Wait until the server has actually mapped the window: plugins that
        // create an EGL/GL surface against this XID (e.g. Vital/BGFX) fail
        // "Failed to set context" if it is still unmapped.
        XSync(display, False);
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
            // Plugins (e.g. LSP) open their own top-level dialogs as transients
            // of the editor/view instead of children of it. Destroy those too,
            // or closing the editor leaves them on screen.
            closeTransientWindows();
            XUngrabKey(display, AnyKey, AnyModifier, window);
            XDestroyWindow(display, window);
            window = 0;
        }
        if (display) {
            XCloseDisplay(display);
            display = nullptr;
        }
    }

    void closeTransientWindows() {
        Window rootRet = 0, parentRet = 0;
        Window* children = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(display, DefaultRootWindow(display), &rootRet, &parentRet, &children, &n))
            return;
        for (unsigned int i = 0; i < n; ++i) {
            Window w = children[i];
            if (!w || w == window) continue;
            Window transientFor = 0;
            if (!XGetTransientForHint(display, w, &transientFor) || !transientFor) continue;
            // Related if the transient points at our editor or at any window in
            // our subtree (the plugin's view lives inside it).
            bool related = (transientFor == window);
            Window t = transientFor;
            while (!related && t) {
                Window rr = 0, pr = 0;
                Window* cc = nullptr;
                unsigned int cn = 0;
                if (!XQueryTree(display, t, &rr, &pr, &cc, &cn)) break;
                if (cc) XFree(cc);
                related = (pr == window);
                t = pr;
            }
            if (related) XDestroyWindow(display, w);
        }
        if (children) XFree(children);
        XSync(display, False);
    }

    void grabKey(KeySym keysym, unsigned int modifiers) {
        if (!display) return;
        int kc = XKeysymToKeycode(display, keysym);
        // owner_events = False: the host sees these keys first, then asks the plugin
        // (IPlugView::onKeyDown) whether it wants them before running its shortcut.
        XGrabKey(display, kc, modifiers, window, False, GrabModeAsync, GrabModeAsync);
        if (modifiers != AnyModifier) {
            // Also grab with lock keys (CapsLock, NumLock, ScrollLock)
            XGrabKey(display, kc, modifiers | LockMask, window, False, GrabModeAsync, GrabModeAsync);
            XGrabKey(display, kc, modifiers | Mod2Mask, window, False, GrabModeAsync, GrabModeAsync); // NumLock
            XGrabKey(display, kc, modifiers | LockMask | Mod2Mask, window, False, GrabModeAsync, GrabModeAsync);
        }
    }

    void* getNativeWindowHandle() override {
        return reinterpret_cast<void*>(window);
    }

    const char* getPlatformType() override {
        return Steinberg::kPlatformTypeX11EmbedWindowID;
    }

    void notifyEmbedded() override {
        if (!display || !window) return;
        Window root = 0, parent = 0, *children = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(display, window, &root, &parent, &children, &n)) return;
        const Atom xembed = XInternAtom(display, "_XEMBED", False);
        for (unsigned int i = 0; i < n; ++i) {
            const Window child = children[i];
            auto sendEmbed = [&](long message, long detail) {
                XEvent ev{};
                ev.xclient.type = ClientMessage;
                ev.xclient.window = child;
                ev.xclient.message_type = xembed;
                ev.xclient.format = 32;
                ev.xclient.data.l[0] = CurrentTime;
                ev.xclient.data.l[1] = message;
                ev.xclient.data.l[2] = detail;
                ev.xclient.data.l[3] = static_cast<long>(window);
                ev.xclient.data.l[4] = 0;
                XSendEvent(display, child, False, NoEventMask, &ev);
            };
            sendEmbed(0, 0);   // XEMBED_EMBEDDED_NOTIFY
            sendEmbed(1, 0);   // XEMBED_WINDOW_ACTIVATE
            sendEmbed(4, 0);   // XEMBED_FOCUS_IN / XEMBED_FOCUS_CURRENT
            XSetInputFocus(display, child, RevertToParent, CurrentTime);
            XRaiseWindow(display, child);
        }
        if (children) XFree(children);
        XFlush(display);
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

    // Ask the plugin whether it wants this key (text fields, its own shortcuts).
    bool pluginConsumesKey(KeySym sym, bool ctrl, bool shift) {
        if (!view) return false;
        Steinberg::char16 ch = 0;
        if (sym == XK_space) ch = ' ';
        else if (sym == XK_z) ch = 'z';
        else if (sym == XK_s) ch = 's';
        Steinberg::int16 modifiers = 0;
        if (ctrl) modifiers |= Steinberg::kControlKey;
        if (shift) modifiers |= Steinberg::kShiftKey;
        return view->onKeyDown(ch, static_cast<Steinberg::int16>(ch), modifiers) == Steinberg::kResultTrue;
    }

    // The grab stole the key from the plugin; replay it on the plugin's window.
    void forwardKeyToPlugin(XKeyEvent& ev) {
        if (!display || !window) return;
        ::Window root = 0, parent = 0, *children = nullptr;
        unsigned int n = 0;
        ::Window target = window;
        if (XQueryTree(display, window, &root, &parent, &children, &n) && children && n > 0) {
            target = children[n - 1];
            XFree(children);
        }
        XKeyEvent ke = ev;
        ke.window = target;
        ke.root = root;
        XSendEvent(display, target, True, KeyPressMask, reinterpret_cast<XEvent*>(&ke));
        XFlush(display);
    }

    void handleGrabbedKey(XKeyEvent& ev) {
        KeySym sym = XLookupKeysym(&ev, 0);
        bool ctrl = (ev.state & ControlMask) != 0;
        bool shift = (ev.state & ShiftMask) != 0;

        // Plugin first: only if it declines does the key act as a host shortcut.
        if (pluginConsumesKey(sym, ctrl, shift)) {
            forwardKeyToPlugin(ev);
            return;
        }

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

    void move(int x, int y) override {
        if (display && window) {
            XMoveWindow(display, window, x, y);
            XRaiseWindow(display, window);
            XSync(display, False);
        }
    }

    void fitPluginWindow(int w, int h) override {
        if (!display || !window) return;
        fitDescendant(window, w, h);
        XSync(display, False);
    }

    void fitDescendant(Window parent, int w, int h) {
        Window rootRet = 0, parentRet = 0;
        Window* children = nullptr;
        unsigned int n = 0;
        if (!XQueryTree(display, parent, &rootRet, &parentRet, &children, &n)) return;
        for (unsigned int i = 0; i < n; ++i) {
            Window child = children[i];
            char* name = nullptr;
            if (XFetchName(display, child, &name) && name) {
                const bool isPluginWindow = std::string(name).find("yabridge plugin") != std::string::npos;
                XFree(name);
                if (isPluginWindow) {
                    XResizeWindow(display, child, static_cast<unsigned int>(w),
                                  static_cast<unsigned int>(h));
                    continue;
                }
            }
            fitDescendant(child, w, h);
        }
        if (children) XFree(children);
    }

    void resize(int w, int h) override {
        if (display && window) {
            XResizeWindow(display, window, w, h);
            XFlush(display);
        }
    }

    bool pointerButtonDown() override {
        if (!display) return false;
        ::Window root, child;
        int rx, ry, wx, wy;
        unsigned int mask = 0;
        XQueryPointer(display, DefaultRootWindow(display), &root, &child, &rx, &ry, &wx, &wy, &mask);
        return (mask & (Button1Mask | Button2Mask | Button3Mask)) != 0;
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

VstPlugin::VstPlugin(const std::string& pluginPath, size_t classIndex) : pluginPath(pluginPath) {
    lib = VstPluginCache::instance().load(pluginPath);
    if (!lib) {
        return;
    }

    const PlugLib::ClassInfo* cls = lib->getClass(classIndex);
    if (!cls) {
        lib = nullptr;
        return;
    }
    classIndex_ = classIndex;
    className_ = cls->name;
    std::memcpy(componentCID_, cls->componentCID, sizeof(Steinberg::TUID));
    std::memcpy(controllerCID_, cls->controllerCID, sizeof(Steinberg::TUID));
    hasController_ = cls->hasController;

    if (!createComponent()) { lib = nullptr; return; }

    name = cls->name;
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
        // the actual parameter values (not all zeros). Skipped while restoring a
        // chain insert: some plugins fault on state I/O before graph setup.
        if (!g_vstLoadingChainInsert) {
            auto compState = getComponentState();
            if (!compState.empty()) applyComponentStateToController(compState);
        }
        refreshParameterShadow();

        editController->setComponentHandler(hostFrame.get());
        view = editController->createView(Steinberg::Vst::ViewType::kEditor);
    }
    valid = true;
}

VstPlugin::~VstPlugin() {
    // May run on the audio thread; the list is mutex-protected so a destroyed
    // plugin can't be dereferenced by the next GUI tick.
    unregisterEditor(this);
    if (view) {
        view->removed();
        view->release();
        view = nullptr;
    }
    editorHost.reset();
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
    hostFrame.reset();
}

bool VstPlugin::createComponent() {
    Steinberg::FUnknown* componentUnknown = nullptr;
    auto result = lib->getFactory()->createInstance(
        componentCID_, Steinberg::Vst::IComponent::iid, (void**)&componentUnknown);

    if (result != Steinberg::kResultTrue || !componentUnknown) {
        std::cerr << "[VstPlugin] Failed to create component" << std::endl;
        return false;
    }

    component = Steinberg::FUnknownPtr<Steinberg::Vst::IComponent>(componentUnknown);
    if (!component) return false;

    if (vstDebug()) std::cout << "[vst] component initialize: " << className_ << std::endl;
    if (component->initialize(&g_hostContext) != Steinberg::kResultTrue) {
        std::cerr << "[VstPlugin] Component initialize failed" << std::endl;
        return false;
    }

    // Query the processor only after the component is initialized: LSP rejects
    // setProcessing() when the processor came from an uninitialized component.
    Steinberg::FUnknown* processorUnknown = nullptr;
    result = component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&processorUnknown);
    if (result != Steinberg::kResultTrue || !processorUnknown) {
        std::cerr << "[VstPlugin] Failed to get IAudioProcessor" << std::endl;
        return false;
    }
    audioProcessor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(processorUnknown);

    return true;
}

bool VstPlugin::createController() {
    if (editController) return true; // already created
    if (!hasController_) return false;
    Steinberg::FUnknown* controllerUnknown = nullptr;
    auto result = lib->getFactory()->createInstance(
        controllerCID_, Steinberg::Vst::IEditController::iid, (void**)&controllerUnknown);

    if (result != Steinberg::kResultTrue || !controllerUnknown) {
        std::cerr << "[VstPlugin] Failed to create edit controller" << std::endl;
        return false;
    }

    editController = Steinberg::FUnknownPtr<Steinberg::Vst::IEditController>(controllerUnknown);
    if (!editController) return false;

    if (vstDebug()) std::cout << "[vst] controller initialize: " << className_ << std::endl;
    if (editController->initialize(&g_hostContext) != Steinberg::kResultTrue) {
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

void VstPlugin::setup(int sampleRate, int bufferSize, const std::function<void()>& beforeActivate) {
    if (!valid || !audioProcessor) return;

    // Both graph copies (and the mixer's per-chain setup) call setup repeatedly. The
    // instance is shared, so a second setupProcessing while processing breaks some
    // plugins (LSP rejects setProcessing and would end up inactive).
    if (processingActive && sampleRate == processSampleRate && bufferSize == processBufferSize)
        return;

    processBufferSize = bufferSize;
    processSampleRate = sampleRate;
    needsSetup_ = false;

    // VST3 requires setupProcessing() while the component is inactive.
    if (processingActive) {
        audioProcessor->setProcessing(false);
        processingActive = false;
    }
    component->setActive(false);

    Steinberg::Vst::ProcessSetup processSetup{};
    processSetup.sampleRate = sampleRate;
    processSetup.maxSamplesPerBlock = bufferSize;
    processSetup.processMode = Steinberg::Vst::kRealtime;
    processSetup.symbolicSampleSize = Steinberg::Vst::kSample32;

    const bool setupDebug = vstDebug();
    if (setupDebug)
        std::cerr << "[setup] setupProcessing sr=" << sampleRate << " bs=" << bufferSize << std::endl;
    if (audioProcessor->setupProcessing(processSetup) != Steinberg::kResultTrue) {
        std::cerr << "[VstPlugin] setupProcessing failed" << std::endl;
        return;
    }
    if (setupDebug)
        std::cerr << "[setup] setupProcessing done" << std::endl;

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

    // Restore saved state while the component is still inactive: state loaded
    // after activation leaves sample-rate/tempo-derived values stale until the
    // user touches the control (LSP tempo-synced delays).
    if (setupDebug)
        std::cerr << "[setup] beforeActivate hook=" << (beforeActivate ? "yes" : "no") << std::endl;
    if (beforeActivate) beforeActivate();

    // setActive/setProcessing results are advisory. Some plugins (LSP) answer
    // kResultFalse/kNotImplemented and process audio anyway; treating that as a
    // failure silences the plugin entirely.
    if (setupDebug) std::cerr << "[setup] setActive" << std::endl;
    component->setActive(true);
    if (setupDebug) std::cerr << "[setup] setProcessing" << std::endl;
    const auto procResult = audioProcessor->setProcessing(true);
    if (procResult != Steinberg::kResultTrue && vstDebug())
        std::cout << "[vst] setProcessing returned " << procResult << " (continuing)" << std::endl;

    processingActive = true;
}

void VstPlugin::processAudio(int bufferSize, Steinberg::Vst::IEventList* inputEvents,
                              double tempo, double projectTimeMusic, double projectTimeSamples,
                              bool isPlaying) {
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
    context.state = Steinberg::Vst::ProcessContext::kProjectTimeMusicValid
                  | Steinberg::Vst::ProcessContext::kTempoValid
                  | Steinberg::Vst::ProcessContext::kTimeSigValid;
    if (isPlaying)
        context.state |= Steinberg::Vst::ProcessContext::kPlaying;
    context.sampleRate = processSampleRate;
    context.tempo = tempo;
    context.projectTimeMusic = projectTimeMusic;
    context.projectTimeSamples = projectTimeSamples;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;
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
                if (!compState.empty()) applyComponentStateToController(compState);
                refreshParameterShadow();

                editController->setComponentHandler(frame);
                view = editController->createView(Steinberg::Vst::ViewType::kEditor);
            }
            if (!view) return;
            view->setFrame(frame);
            std::cerr << "[showEditor] yabridge bg: attached..." << std::endl;
            view->attached(nativeHandle, platformType);
            // The plugin creates its own (often large) child window; fit the
            // host to the view and put it somewhere visible.
            Steinberg::ViewRect vr{};
            if (view->getSize(&vr) == Steinberg::kResultTrue && vr.getWidth() > 0 && vr.getHeight() > 0) {
                eh->resize(vr.getWidth(), vr.getHeight());
                view->onSize(&vr);
                eh->fitPluginWindow(vr.getWidth(), vr.getHeight());
            }
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
        if (pluginPath.find("lsp-plugins") != std::string::npos)
            editorHost->notifyEmbedded();
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

void* VstPlugin::getEditorWindowHandle() const {
    if (!editorOpen || !editorHost) return nullptr;
    return editorHost->getNativeWindowHandle();
}

bool VstPlugin::tickEditor() {
    if (!editorOpen || !editorHost) return false;

    // Track the pointer press/release before draining plugin events, so the
    // performEdits below can be grouped into one drag (plugins that skip
    // beginEdit/endEdit, e.g. LSP).
    if (hostFrame) {
        const bool down = editorHost->pointerButtonDown();
        if (down && !hostFrame->pointerIsDown) {
            // A new press is a drag: don't let its edits merge into a preceding
            // wheel/key run of the same parameter.
            hostFrame->dragSession++;
            hostFrame->freeGestures.clear();
        }
        hostFrame->pointerIsDown = down;
    }

    // Poll runloop — fires plugin timers and FD events
    if (hostFrame) hostFrame->pollRunLoop();

    // Preset load / non-parameter state change: diff the state blobs now so the
    // change becomes an undo entry without waiting for the editor to close.
    if (hostFrame && hostFrame->stateResetRequested) {
        hostFrame->stateResetRequested = false;
        hostFrame->statePollRequested = false;
        if (hostFrame->onStateReset) hostFrame->onStateReset();
    }

    if (!editorHost->tick()) {
        // Final state check so edits made just before closing the window still
        // land in the undo tree.
        if (hostFrame && hostFrame->onStatePoll) hostFrame->onStatePoll(true);
        if (view) view->removed();
        editorHost.reset();
        editorOpen = false;
        unregisterEditor(this);
        return false;
    }
    // Periodic getState() polling while the editor is open crashes inside some
    // plugins (Vital: va::Sample::stateToJson dereferences null). Capture the state
    // when the editor closes instead (see the onStatePoll call above).
    return true;
}

// Per-node instance cache — a single VstPlugin shared across GUI + audio copies.
std::shared_ptr<VstPlugin> VstPluginCache::getOrCreatePlugin(int nodeID, const std::vector<int>& managerPath,
                                                            const std::string& path, size_t classIndex,
                                                            int instance) {
    CacheKey key{nodeID, managerPath, instance};
    auto it = instances.find(key);
    if (it != instances.end()) {
        if (it->second->getPluginPath() == path && it->second->getClassIndex() == classIndex)
            return it->second;
        instances.erase(it);
    }
    if (path.empty()) return nullptr;
    auto p = std::make_shared<VstPlugin>(path, classIndex);
    if (!p->isValid()) return nullptr;
    instances[key] = p;
    return p;
}

void VstPluginCache::removePlugin(int nodeID, const std::vector<int>& managerPath) {
    for (auto it = instances.begin(); it != instances.end();) {
        if (std::get<0>(it->first) == nodeID && std::get<1>(it->first) == managerPath)
            it = instances.erase(it);
        else
            ++it;
    }
}

// Static editor tick — call from main event loop. Plugins can be destroyed on
// the audio thread (undo replay), so entries are removed under the mutex and
// iteration runs over a copy.
static std::vector<VstPlugin*> openEditors;
// Recursive: a plugin destroyed on the audio thread waits for the current GUI
// tick, and a tick may unregister the plugin it is ticking.
static std::recursive_mutex openEditorsMutex;

void VstPlugin::registerEditor(VstPlugin* p) {
    std::lock_guard<std::recursive_mutex> lk(openEditorsMutex);
    if (std::find(openEditors.begin(), openEditors.end(), p) == openEditors.end())
        openEditors.push_back(p);
}

void VstPlugin::unregisterEditor(VstPlugin* p) {
    std::lock_guard<std::recursive_mutex> lk(openEditorsMutex);
    openEditors.erase(std::remove(openEditors.begin(), openEditors.end(), p), openEditors.end());
}

void VstPlugin::tickAllEditors() {
    std::lock_guard<std::recursive_mutex> lk(openEditorsMutex);
    // The lock is held while ticking so a plugin destroyed on the audio thread
    // cannot be freed mid-dereference. Index iteration, because tickEditor may
    // unregister the entry it is ticking.
    for (size_t i = 0; i < openEditors.size(); ) {
        VstPlugin* p = openEditors[i];
        if (p->editorOpen) p->tickEditor();
        if (i < openEditors.size() && openEditors[i] == p) ++i;
    }
}

void VstPlugin::commitPendingStateEdits() {
    std::lock_guard<std::recursive_mutex> lk(openEditorsMutex);
    for (size_t i = 0; i < openEditors.size(); ) {
        VstPlugin* p = openEditors[i];
        if (p->editorOpen && p->undoStatePoll_ && p->hostFrame && p->hostFrame->onStatePoll)
            p->hostFrame->onStatePoll(true);
        if (i < openEditors.size() && openEditors[i] == p) ++i;
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

int VstPlugin::getParameterID(int paramIndex) const {
    if (!editController) return -1;
    Steinberg::Vst::ParameterInfo info{};
    if (editController->getParameterInfo(paramIndex, info) != Steinberg::kResultOk) return -1;
    return static_cast<int>(info.id);
}

std::string VstPlugin::getParameterNameByID(int paramID) const {
    if (editController) {
        int count = getParameterCount();
        Steinberg::Vst::ParameterInfo info{};
        for (int i = 0; i < count; ++i) {
            if (editController->getParameterInfo(i, info) == Steinberg::kResultOk &&
                info.id == static_cast<Steinberg::Vst::ParamID>(paramID))
                return getParameterName(i);
        }
    }
    return "Param " + std::to_string(paramID);
}

std::string VstPlugin::getParameterName(int paramIndex) const {
    if (!editController) return "Param " + std::to_string(paramIndex);
    Steinberg::Vst::ParameterInfo info{};
    if (editController->getParameterInfo(paramIndex, info) == Steinberg::kResultOk) {
        std::string result;
        for (int i = 0; i < 128 && info.title[i] != 0; ++i) {
            char16_t c = info.title[i];
            if (c < 0x80) {
                result += static_cast<char>(c);
            } else if (c < 0x800) {
                result += static_cast<char>(0xC0 | (c >> 6));
                result += static_cast<char>(0x80 | (c & 0x3F));
            } else {
                result += static_cast<char>(0xE0 | (c >> 12));
                result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return result;
    }
    return "Param " + std::to_string(paramIndex);
}

void VstPlugin::setParameterValue(int paramID, float valueNormalized) {
    if (!editController || !hostFrame) return;
    editController->setParamNormalized(static_cast<Steinberg::Vst::ParamID>(paramID),
                                       static_cast<Steinberg::Vst::ParamValue>(valueNormalized));
    // The controller call above only updates the plugin GUI; the processor
    // hears the change via inputParameterChanges on the next audio block.
    queueParameterChange(static_cast<Steinberg::Vst::ParamID>(paramID),
                         static_cast<Steinberg::Vst::ParamValue>(valueNormalized));
    // Ardour's set_parameter() updates _shadow_data; do the same so the next
    // editor performEdit sees the value the host just wrote.
    setShadowParameterValue(paramID, valueNormalized);
}

float VstPlugin::shadowParameterValue(int paramID) const {
    {
        std::lock_guard<std::mutex> lk(paramShadowMutex_);
        auto it = paramShadow_.find(paramID);
        if (it != paramShadow_.end()) return it->second;
    }
    return getParameterValue(paramID);
}

void VstPlugin::setShadowParameterValue(int paramID, float valueNormalized) {
    std::lock_guard<std::mutex> lk(paramShadowMutex_);
    paramShadow_[paramID] = valueNormalized;
}

float VstPlugin::swapShadowParameterValue(int paramID, float valueNormalized) {
    const float fallback = getParameterValue(paramID);
    std::lock_guard<std::mutex> lk(paramShadowMutex_);
    auto it = paramShadow_.find(paramID);
    const float previous = it != paramShadow_.end() ? it->second : fallback;
    paramShadow_[paramID] = valueNormalized;
    return previous;
}

void VstPlugin::refreshParameterShadow() {
    if (!editController) return;
    std::vector<std::pair<int, float>> values;
    const int n = getParameterCount();
    values.reserve(static_cast<size_t>(n > 0 ? n : 0));
    for (int i = 0; i < n; ++i) {
        const int pid = getParameterID(i);
        if (pid >= 0) values.emplace_back(pid, getParameterValue(pid));
    }
    std::lock_guard<std::mutex> lk(paramShadowMutex_);
    paramShadow_.clear();
    for (auto& [pid, v] : values) paramShadow_[pid] = v;
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
    // The controller mirrors the component state (Ardour: setState(component),
    // then controller->setComponentState(same stream)).
    applyComponentStateToController(data);
    refreshParameterShadow();
}

void VstPlugin::beginStateRestore() {
    if (inStateRestore_) return;
    inStateRestore_ = true;
    stateRestoreWasProcessing_ = processingActive;
    if (audioProcessor && processingActive) {
        audioProcessor->setProcessing(false);
        processingActive = false;
    }
    if (component) component->setActive(false);
}

void VstPlugin::endStateRestore() {
    if (!inStateRestore_) return;
    inStateRestore_ = false;
    if (!component) return;
    component->setActive(true);
    if (audioProcessor && stateRestoreWasProcessing_) {
        audioProcessor->setProcessing(true);
        processingActive = true;
    }
    stateRestoreWasProcessing_ = false;
    refreshParameterShadow();
}

void VstPlugin::applyComponentStateToController(const std::vector<uint8_t>& data) {
    if (!editController || data.empty()) return;
    Steinberg::MemoryStream stream;
    Steinberg::int32 bytesWritten = 0;
    stream.write(const_cast<uint8_t*>(data.data()), static_cast<Steinberg::int32>(data.size()), &bytesWritten);
    Steinberg::int64 resultPos = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, &resultPos);
    editController->setComponentState(&stream);
}

void VstPlugin::setControllerState(const std::vector<uint8_t>& data) {
    if (!editController || data.empty()) return;
    Steinberg::MemoryStream stream;
    Steinberg::int32 bytesWritten = 0;
    stream.write(const_cast<uint8_t*>(data.data()), static_cast<Steinberg::int32>(data.size()), &bytesWritten);
    Steinberg::int64 resultPos = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, &resultPos);
    // Controller state is restored with setState(). setComponentState() expects the
    // COMPONENT's state blob; feeding it controller state corrupts plugins (LSP logs
    // "Invalid state signature" and double-frees).
    editController->setState(&stream);
    refreshParameterShadow();
}

#endif // __EMSCRIPTEN__
