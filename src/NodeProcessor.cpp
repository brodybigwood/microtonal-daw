#include "NodeProcessor.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include "nodes/patcher/patcher.h"
#include <cstring>

NodeProcessor::NodeProcessor(Project* p) : project(p) {
    manager = new NodeManager(project);
    editor = new NodeEditor;
    editor->window = SDL_CreateWindow("NodeProcessorHost", 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_UTILITY);
    editor->renderer = SDL_CreateRenderer(editor->window, NULL);
    manager->setNE(editor);
    auto* root = dynamic_cast<PatcherNode*>(manager->addNodeNow(NodeType::Patcher, 140.0f, 140.0f));
    setNode(root);
    if (root && !root->detached) root->detach();
}

NodeProcessor::~NodeProcessor() {
    if (manager) manager->resetNE();
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
    delete manager;
    manager = nullptr;
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
    if (!manager) return json::object();
    return manager->serialize();
}

void NodeProcessor::deSerialize(const json& j) {
    if (!manager) return;
    manager->deSerialize(j);
    for (auto* n : manager->getNodes()) {
        auto* p = dynamic_cast<PatcherNode*>(n);
        if (p) {
            setNode(p);
            return;
        }
    }
    auto* root = dynamic_cast<PatcherNode*>(manager->addNodeNow(NodeType::Patcher, 140.0f, 140.0f));
    setNode(root);
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

    if (!node) {
        std::memset(out, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(numOut) * sizeof(float));
        return;
    }

    node->update(bufferSize, sampleRate);
    int inCh = 0;
    for (auto* c : node->inputs.connections) {
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

    node->process();

    int outCh = 0;
    for (auto* c : node->outputs.connections) {
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
