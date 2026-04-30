#include "patcher.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include <cstring>

PatcherNode::PatcherNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Patcher) {
    auto path = nm->managerPath;
    path.push_back(id);
    mainManager = new NodeManager(project, path);
    ensureOutputChannels(mainManager->outNode->inputs.connections.size());
}

PatcherNode::~PatcherNode() {
    attachFinal();
    if (mainManager) {
        delete mainManager;
        mainManager = nullptr;
    }
}

void PatcherNode::process() {
    if (outputs.connections.empty()) return;
    int bs = bufferSize;
    int sr = sampleRate;
    int channels = static_cast<int>(outputs.connections.size());

    if (channels <= 0 || bs <= 0) return;

    patchBuffer.resize(static_cast<size_t>(channels) * bs, 0.0f);
    mainManager->process(patchBuffer.data(), bs, channels, sr);

    for (int ch = 0; ch < channels; ++ch) {
        auto out = outputs.connections[ch];
        if (!out || !out->buffer) continue;
        std::memcpy(out->buffer, patchBuffer.data() + static_cast<size_t>(ch) * bs, bs * sizeof(float));
    }
}

void PatcherNode::renderPresent() {
    if (detached) {
        if (mainEditor) mainEditor->renderPresent();
    }
}

void PatcherNode::renderContent(SDL_Renderer*) {

    if (mainEditor) {
        mainEditor->tick();
    } else {
        if (!vCount) {
            vCount = 4;
            vx = new float[vCount];
            vy = new float[vCount];
    
            vx[0] = 0;
            vx[1] = TEX_W;
            vx[2] = TEX_W-300;
            vx[3] = 300;
    
            vy[0] = 0;
            vy[1] = 0;
            vy[2] = TEX_H;
            vy[3] = TEX_H;
        }
    
        filledPolygonRGBA(renderer, vx, vy, vCount, 50, 50, 50, 255);
        aapolygonRGBA(renderer, vx, vy, vCount, 0, 0, 0, 255);

        renderParams(renderer);
    }
}

void PatcherNode::attachFinal() {
    mainManager->resetNE();
    if (mainEditor) delete mainEditor;
    mainEditor = nullptr;
}

void PatcherNode::detachFinal() {
    mainEditor = new NodeEditor;
    mainEditor->window = window;
    mainEditor->renderer = renderer;
    mainManager->setNE(mainEditor);
    mainEditor->retach();
}

bool PatcherNode::handleCustomInput(SDL_Event& e) {
    if (mainEditor) mainEditor->handleWindowInput(e);
    return false;
}

void PatcherNode::clearCustomTextures() {
    if (neTex) SDL_DestroyTexture(neTex);
    neTex = nullptr;
}

json PatcherNode::extraSerialize() {
    json j;
    j["mainManager"] = mainManager->serialize();
    return j;
}

void PatcherNode::extraDeSerialize(json j) {
    if (!j.contains("mainManager")) return;
    mainManager->deSerialize(j["mainManager"]);
    ensureOutputChannels(mainManager->outNode->inputs.connections.size());
}

void PatcherNode::ensureOutputChannels(size_t count) {
    if (count < 1) count = 1;
    while (outputs.connections.size() < count) {
        auto c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::output;
        outputs.addConnection(c);
    }
    while (outputs.connections.size() > count) {
        auto* c = outputs.connections.back();
        if (c->is_connected) break;
        outputs.id_pool.releaseID(c->id);
        outputs.ids.erase(c->id);
        outputs.connections.pop_back();
        delete c;
    }
    makeConnectionRects();
}
