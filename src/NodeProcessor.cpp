#include "NodeProcessor.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include "WindowManager.h"
#include <cstring>

NodeProcessor::NodeProcessor(Project* p) : project(p) {
    editor = new NodeEditor;
    editor->enableRootMenuBar();
    hostWindow = SDL_CreateWindow("NodeProcessorHost", 1920, 1080, SDL_WINDOW_RESIZABLE);
    hostRenderer = SDL_CreateRenderer(hostWindow, NULL);

    // GUI copy: owns SDL resources, used for rendering/editing.
    guiGraph = new NodeManager(project);
    guiGraph->setNE(editor);
    setThreadActiveRoot(guiGraph);

    // Audio copy: separate project graph, no SDL resources.
    dspGraph = new NodeManager(project);

    // Expanded window manager for pop-out windows (PianoRoll, Preferences, etc.)
    windowManager = new WindowManager;
}

NodeProcessor::~NodeProcessor() {
    // Destroy expanded windows before tearing down SDL resources.
    delete windowManager;
    windowManager = nullptr;

    if (guiGraph) {
        guiGraph->resetNE();
        delete guiGraph;
        guiGraph = nullptr;
    }
    if (dspGraph) {
        dspGraph->resetNE();
        delete dspGraph;
        dspGraph = nullptr;
    }
    if (hostRenderer) {
        SDL_DestroyRenderer(hostRenderer);
        hostRenderer = nullptr;
    }
    if (hostWindow) {
        SDL_DestroyWindow(hostWindow);
        hostWindow = nullptr;
    }
    delete editor;
    editor = nullptr;
}

json NodeProcessor::serialize() const {
    if (!guiGraph) return json::object();
    return guiGraph->serialize();
}

void NodeProcessor::deSerialize(const json& j) {
    if (!editor) return;

    // Backward compat: old format wrapped the real graph inside a root PatcherNode's
    // mainManager. Only copy the DOM when that rewrite is actually needed.
    const json* src = &j;
    json transformed;
    if (j.contains("nodes") && j["nodes"].is_array() && j["nodes"].size() == 1) {
        const auto& n = j["nodes"][0];
        if (n.value("nodeType", -1) == static_cast<int>(NodeType::Patcher) &&
            n.contains("extra") && n["extra"].contains("mainManager")) {
            const auto& inner = n["extra"]["mainManager"];
            transformed = j;
            transformed["nodes"] = inner["nodes"];
            transformed["connections"] = inner["connections"];
            transformed["idManager"] = inner["idManager"];
            // Keep top-level outNode/inNode (they have 2 channels vs inner's 0).
            src = &transformed;
        }
    }

    // Wipe and rebuild GUI copy.
    if (guiGraph) {
        guiGraph->resetNE();
        delete guiGraph;
        guiGraph = nullptr;
    }
    guiGraph = new NodeManager(project);
    guiGraph->setNE(editor);
    guiGraph->deSerialize(*src);

    // Wipe and rebuild audio copy from the same JSON.
    if (dspGraph) {
        dspGraph->resetNE();
        delete dspGraph;
        dspGraph = nullptr;
    }
    dspGraph = new NodeManager(project);
    dspGraph->deSerialize(*src);

    setThreadActiveRoot(guiGraph);
}

void NodeProcessor::render() {
    flushProcessorActions();
    if (editor) editor->tick(project->renderer);
}

void NodeProcessor::renderPresent() {
    if (editor) editor->renderPresent(hostRenderer);
}

void NodeProcessor::enqueueProcessorAction(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(processorActionMutex_);
    pendingProcessorActions_.push_back(std::move(fn));
}

void NodeProcessor::flushProcessorActions() {
    std::vector<std::function<void()>> actions;
    {
        std::lock_guard<std::mutex> lock(processorActionMutex_);
        actions.swap(pendingProcessorActions_);
    }
    for (auto& fn : actions) fn();
}

void NodeProcessor::handleWindowInput(SDL_Event& e) {
    if (editor) editor->handleWindowInput(e);
}

void NodeProcessor::process(float* in, float* out, int numIn, int numOut, int bufferSize, int sampleRate) {
    if (!out || bufferSize <= 0 || numOut <= 0) return;

    NodeManager* mgr = dspGraph;
    if (!mgr) {
        std::memset(out, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(numOut) * sizeof(float));
        return;
    }

    mgr->inNode->input = in;
    mgr->inNode->numChannels = numIn;

    int bs = bufferSize;
    int nc = numOut;
    int sr = sampleRate;
    mgr->process(out, bs, nc, sr);
}
