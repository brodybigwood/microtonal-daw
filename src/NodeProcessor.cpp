#include "NodeProcessor.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include "nodes/patcher/patcher.h"
#include <cstring>

NodeProcessor::NodeProcessor(Project* p) : project(p) {
    editor = new NodeEditor;
    editor->window = SDL_CreateWindow("NodeProcessorHost", 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_UTILITY);
    editor->renderer = SDL_CreateRenderer(editor->window, NULL);

    // GUI copy: owns SDL resources, used for rendering/editing.
    guiManager = new NodeManager(project);
    guiManager->setNE(editor);
    auto* root = dynamic_cast<PatcherNode*>(guiManager->addNodeNow(NodeType::Patcher, 140.0f, 140.0f));
    setNode(root);
    if (root && !root->detached) root->detach();
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
    auto* rootPatcher = dynamic_cast<PatcherNode*>(node);
    if (rootPatcher && rootPatcher->ne && rootPatcher->renderer && !rootPatcher->detached) {
        rootPatcher->detach();
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

    // Use audio copy for DSP.
    NodeManager* mgr = audioManager;
    if (!mgr) {
        std::memset(out, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(numOut) * sizeof(float));
        return;
    }

    // Resolve audio copy root patcher.
    PatcherNode* root = nullptr;
    for (auto* n : mgr->getNodes()) {
        root = dynamic_cast<PatcherNode*>(n);
        if (root) break;
    }
    if (!root) {
        std::memset(out, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(numOut) * sizeof(float));
        return;
    }

    root->update(bufferSize, sampleRate);
    int inCh = 0;
    for (auto* c : root->inputs.connections) {
        if (!c || c->type != DataType::Waveform) continue;
        if (in && inCh < numIn) {
            c->is_connected = true;
            c->buffer = in + static_cast<size_t>(inCh) * static_cast<size_t>(bufferSize);
            c->bufferSize = bufferSize;
            c->input_node = -1;
            c->input_connection = -1;
            c->events = nullptr;
            ++inCh;
        } else {
            c->is_connected = false;
            c->buffer = nullptr;
            c->bufferSize = 0;
            c->input_node = -1;
            c->input_connection = -1;
            c->events = nullptr;
        }
    }

    root->process();

    int outCh = 0;
    for (auto* c : root->outputs.connections) {
        if (!c || c->type != DataType::Waveform) continue;
        if (outCh >= numOut) break;
        float* dst = out + static_cast<size_t>(outCh) * static_cast<size_t>(bufferSize);
        if (c->buffer) {
            std::memcpy(dst, c->buffer, static_cast<size_t>(bufferSize) * sizeof(float));
        } else {
            std::memset(dst, 0, static_cast<size_t>(bufferSize) * sizeof(float));
        }
        ++outCh;
    }
    while (outCh < numOut) {
        float* dst = out + static_cast<size_t>(outCh) * static_cast<size_t>(bufferSize);
        std::memset(dst, 0, static_cast<size_t>(bufferSize) * sizeof(float));
        ++outCh;
    }
}
