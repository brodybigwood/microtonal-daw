#include "NodeProcessor.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include "nodes/patcher/patcher.h"
#include <cstring>

NodeProcessor::NodeProcessor(Project* p) : project(p) {
    editor = new NodeEditor;
    editor->window = SDL_CreateWindow("NodeProcessorHost", 1920, 1080, SDL_WINDOW_RESIZABLE);
    editor->renderer = SDL_CreateRenderer(editor->window, NULL);

    // GUI copy: owns SDL resources, used for rendering/editing.
    guiManager = new NodeManager(project);
    guiManager->setNE(editor);
    auto* root = dynamic_cast<PatcherNode*>(guiManager->addNodeNow(NodeType::Patcher, 140.0f, 140.0f));
    setNode(root);
    setThreadActiveRoot(guiManager);

    // Audio copy: separate project graph, no SDL resources.
    audioManager = new NodeManager(project);
    {
        // Clone via serialize/deserialize so audio copy starts identical.
        json snapshot = guiManager->serialize();
        audioManager->deSerialize(snapshot);
    }

    // Patch up root node pointer to point at GUI copy's root (for getRootPatcher, etc.).
    for (auto* n : guiManager->getNodes()) {
        auto* pn = dynamic_cast<PatcherNode*>(n);
        if (pn) {
            setNode(pn);
            break;
        }
    }
}

NodeProcessor::~NodeProcessor() {
    if (guiManager) {
        guiManager->resetNE();
        delete guiManager;
        guiManager = nullptr;
    }
    if (audioManager) {
        audioManager->resetNE();
        delete audioManager;
        audioManager = nullptr;
    }
    if (editor && editor->renderer) {
        SDL_DestroyRenderer(editor->renderer);
        editor->renderer = nullptr;
    }
    if (editor && editor->window) {
        SDL_DestroyWindow(editor->window);
        editor->window = nullptr;
    }
    delete editor;
    editor = nullptr;
    node = nullptr;
}

PatcherNode* NodeProcessor::getRootPatcher() const {
    return dynamic_cast<PatcherNode*>(node);
}

SDL_Window* NodeProcessor::getHostWindow() const {
    return editor ? editor->window : nullptr;
}

SDL_Renderer* NodeProcessor::getHostRenderer() const {
    return editor ? editor->renderer : nullptr;
}

json NodeProcessor::serialize() const {
    if (!guiManager) return json::object();
    return guiManager->serialize();
}

void NodeProcessor::deSerialize(const json& j) {
    if (!editor) return;

    // Wipe and rebuild GUI copy.
    if (guiManager) {
        guiManager->resetNE();
        delete guiManager;
        guiManager = nullptr;
    }
    guiManager = new NodeManager(project);
    guiManager->setNE(editor);
    guiManager->deSerialize(j);

    // Wipe and rebuild audio copy from the same JSON.
    if (audioManager) {
        audioManager->resetNE();
        delete audioManager;
        audioManager = nullptr;
    }
    audioManager = new NodeManager(project);
    audioManager->deSerialize(j);

    // Active root defaults to GUI.
    setThreadActiveRoot(guiManager);

    // Find root patcher in GUI copy.
    node = nullptr;
    for (auto* n : guiManager->getNodes()) {
        auto* p = dynamic_cast<PatcherNode*>(n);
        if (p) {
            setNode(p);
            break;
        }
    }
    if (!node) {
        auto* root = dynamic_cast<PatcherNode*>(guiManager->addNodeNow(NodeType::Patcher, 140.0f, 140.0f));
        setNode(root);
        // Clone to audio.
        if (audioManager) {
            json snap = guiManager->serialize();
            audioManager->deSerialize(snap);
        }
    }
}

void NodeProcessor::render() {
    if (editor) editor->tick();
}

void NodeProcessor::renderPresent() {
    if (editor) editor->renderPresent();
}

void NodeProcessor::handleWindowInput(SDL_Event& e) {
    if (editor) editor->handleWindowInput(e);
}

void NodeProcessor::process(float* in, float* out, int numIn, int numOut, int bufferSize, int sampleRate) {
    if (!out || bufferSize <= 0 || numOut <= 0) return;

    NodeManager* mgr = audioManager;
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
