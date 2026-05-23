#include "patcher.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include "OutputNode.h"
#include "InputNode.h"
#include "Preferences.h"
#include "nodes/multiplexer/multiplexer.h"
#include <cstring>
#include <iostream>
#include <vector>

PatcherNode::PatcherNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Patcher) {
    auto path = nm->managerPath;
    path.push_back(id);
    mainManager = new NodeManager(project, path);
    mainManager->inNode->setCoupledPatcher(this);
    mainManager->outNode->setCoupledPatcher(this);

    mainEditor = new NodeEditor;
    mainEditor->embedded_ = true;
    mainEditor->setEmbeddedCanvasSize(static_cast<float>(TEX_W), static_cast<float>(TEX_H));
    mainManager->setNE(mainEditor);
    mainEditor->retach();
}

PatcherNode::~PatcherNode() {
    if (mainEditor) {
        mainEditor->setTopMenuBarHostNode(nullptr);
        mainEditor->clearWireDragState();
        mainEditor->retach();
    }
    if (mainEditor) {
        mainManager->resetNE();
        delete mainEditor;
        mainEditor = nullptr;
    }
    if (mainManager) {
        mainManager->inNode->setCoupledPatcher(nullptr);
        mainManager->outNode->setCoupledPatcher(nullptr);
        delete mainManager;
        mainManager = nullptr;
    }
}

size_t PatcherNode::leadingWaveformOutputCount() const {
    size_t i = 0;
    while (i < outputs.connections.size() && outputs.connections[i]->type == DataType::Waveform) {
        ++i;
    }
    return i;
}

void PatcherNode::insertWaveformOutputAt(int index) {
    if (index < 0 || static_cast<size_t>(index) > outputs.connections.size()) return;

    auto* c = new Connection;
    c->nm = outputs.nm;
    c->id = outputs.id_pool.newID();
    c->type = DataType::Waveform;
    c->dir = Direction::output;
    c->is_connected = false;
    c->input_node = id;
    c->input_connection = c->id;
    c->output_node = -1;
    c->output_connection = -1;
    c->events = nullptr;
    c->bufferSize = bufferSize;
    if (bufferSize > 0) {
        c->buffer = new float[bufferSize];
        std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) * sizeof(float));
    } else {
        c->buffer = nullptr;
    }

    outputs.connections.insert(outputs.connections.begin() + index, c);
    outputs.ids.clear();
    for (size_t j = 0; j < outputs.connections.size(); ++j) {
        outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
}

void PatcherNode::removeWaveformOutputAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= outputs.connections.size()) return;
    Connection* c = outputs.connections[static_cast<size_t>(index)];
    if (c->type != DataType::Waveform) return;
    if (c->is_connected) return;

    if (c->buffer) {
        delete[] c->buffer;
        c->buffer = nullptr;
    }
    outputs.id_pool.releaseID(c->id);
    outputs.connections.erase(outputs.connections.begin() + index);
    delete c;

    outputs.ids.clear();
    for (size_t j = 0; j < outputs.connections.size(); ++j) {
        outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
}

void PatcherNode::insertLinkedWaveformAtEndOfBlock() {
    insertWaveformOutputAt(static_cast<int>(leadingWaveformOutputCount()));
}

void PatcherNode::removeLastLinkedWaveformFromBlock() {
    const size_t n = leadingWaveformOutputCount();
    if (n == 0) return;
    removeWaveformOutputAt(static_cast<int>(n - 1));
}

void PatcherNode::setLinkedWaveformChannelCount(size_t count) {
    while (leadingWaveformOutputCount() > count) {
        const size_t before = leadingWaveformOutputCount();
        removeLastLinkedWaveformFromBlock();
        if (leadingWaveformOutputCount() >= before)
            break;
    }
    while (leadingWaveformOutputCount() < count) {
        insertLinkedWaveformAtEndOfBlock();
    }
    if (multiplexer) multiplexer->syncPortsFromPatchers();
}

size_t PatcherNode::trailingEventOutputCount() const {
    const size_t wf = leadingWaveformOutputCount();
    if (outputs.connections.size() <= wf) return 0;
    return outputs.connections.size() - wf;
}

void PatcherNode::appendEventOutput() {
    auto c = new Connection;
    c->type = DataType::Events;
    c->dir = Direction::output;
    outputs.addConnection(c);
    makeConnectionRects();
    nm->markTopologyDirty();
}

void PatcherNode::removeLastTrailingEventOutput() {
    for (int i = static_cast<int>(outputs.connections.size()) - 1; i >= 0; --i) {
        Connection* c = outputs.connections[static_cast<size_t>(i)];
        if (c->type != DataType::Events) continue;
        if (c->is_connected) return;
        if (c->events) {
            delete c->events;
            c->events = nullptr;
        }
        outputs.id_pool.releaseID(c->id);
        outputs.connections.erase(outputs.connections.begin() + i);
        delete c;
        outputs.ids.clear();
        for (size_t j = 0; j < outputs.connections.size(); ++j) {
            outputs.ids[outputs.connections[j]->id] = static_cast<uint16_t>(j);
        }
        makeConnectionRects();
        nm->markTopologyDirty();
        return;
    }
}

void PatcherNode::setLinkedEventOutputCount(size_t count) {
    while (trailingEventOutputCount() > count) {
        const size_t before = trailingEventOutputCount();
        removeLastTrailingEventOutput();
        if (trailingEventOutputCount() >= before)
            break;
    }
    while (trailingEventOutputCount() < count) {
        appendEventOutput();
    }
    if (multiplexer) multiplexer->syncPortsFromPatchers();
}

size_t PatcherNode::leadingWaveformInputCount() const {
    size_t i = 0;
    while (i < inputs.connections.size() && inputs.connections[i]->type == DataType::Waveform) {
        ++i;
    }
    return i;
}

void PatcherNode::insertWaveformInputAt(int index) {
    if (index < 0 || static_cast<size_t>(index) > inputs.connections.size()) return;
    auto* c = new Connection;
    c->nm = inputs.nm;
    c->id = inputs.id_pool.newID();
    c->type = DataType::Waveform;
    c->dir = Direction::input;
    c->is_connected = false;
    c->output_connection = c->id;
    c->output_node = inputs.nodeID;
    c->input_connection = -1;
    c->input_node = -1;
    c->events = nullptr;
    c->buffer = nullptr;
    c->bufferSize = 0;
    inputs.connections.insert(inputs.connections.begin() + index, c);
    inputs.ids.clear();
    for (size_t j = 0; j < inputs.connections.size(); ++j) {
        inputs.ids[inputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
}

void PatcherNode::removeWaveformInputAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= inputs.connections.size()) return;
    Connection* c = inputs.connections[static_cast<size_t>(index)];
    if (c->type != DataType::Waveform) return;
    if (c->is_connected) return;
    inputs.id_pool.releaseID(c->id);
    inputs.connections.erase(inputs.connections.begin() + index);
    delete c;
    inputs.ids.clear();
    for (size_t j = 0; j < inputs.connections.size(); ++j) {
        inputs.ids[inputs.connections[j]->id] = static_cast<uint16_t>(j);
    }
    makeConnectionRects();
}

size_t PatcherNode::trailingEventInputCount() const {
    const size_t wf = leadingWaveformInputCount();
    if (inputs.connections.size() <= wf) return 0;
    return inputs.connections.size() - wf;
}

void PatcherNode::appendEventInput() {
    auto* c = new Connection;
    c->type = DataType::Events;
    c->dir = Direction::input;
    inputs.addConnection(c);
    makeConnectionRects();
    nm->markTopologyDirty();
}

void PatcherNode::removeLastTrailingEventInput() {
    for (int i = static_cast<int>(inputs.connections.size()) - 1; i >= 0; --i) {
        Connection* c = inputs.connections[static_cast<size_t>(i)];
        if (c->type != DataType::Events) continue;
        if (c->is_connected) return;
        inputs.id_pool.releaseID(c->id);
        inputs.connections.erase(inputs.connections.begin() + i);
        delete c;
        inputs.ids.clear();
        for (size_t j = 0; j < inputs.connections.size(); ++j) {
            inputs.ids[inputs.connections[j]->id] = static_cast<uint16_t>(j);
        }
        makeConnectionRects();
        nm->markTopologyDirty();
        return;
    }
}

void PatcherNode::setLinkedWaveformInputCount(size_t count) {
    while (leadingWaveformInputCount() > count) {
        const size_t before = leadingWaveformInputCount();
        removeWaveformInputAt(static_cast<int>(leadingWaveformInputCount() - 1));
        if (leadingWaveformInputCount() >= before)
            break;
    }
    while (leadingWaveformInputCount() < count) {
        insertWaveformInputAt(static_cast<int>(leadingWaveformInputCount()));
    }
    if (multiplexer) multiplexer->syncPortsFromPatchers();
}

void PatcherNode::setLinkedEventInputCount(size_t count) {
    while (trailingEventInputCount() > count) {
        const size_t before = trailingEventInputCount();
        removeLastTrailingEventInput();
        if (trailingEventInputCount() >= before)
            break;
    }
    while (trailingEventInputCount() < count) {
        appendEventInput();
    }
    if (multiplexer) multiplexer->syncPortsFromPatchers();
}

void PatcherNode::process() {
    int bs = bufferSize;
    int sr = sampleRate;
    if (bs <= 0) return;

    int innerInWfCh = static_cast<int>(mainManager->inNode->countWaveformOutputs());
    int innerWfCh = static_cast<int>(mainManager->outNode->countWaveformInputs());
    if (innerInWfCh < 0) innerInWfCh = 0;
    if (innerWfCh < 1) innerWfCh = 1;

    inputPatchBuffer.assign(static_cast<size_t>(innerInWfCh) * static_cast<size_t>(bs), 0.0f);
    int inIndex = 0;
    for (auto* c : inputs.connections) {
        if (!c || c->type != DataType::Waveform) continue;
        if (inIndex >= innerInWfCh) break;
        float* dst = inputPatchBuffer.data() + static_cast<size_t>(inIndex) * static_cast<size_t>(bs);
        if (c->is_connected && c->buffer) {
            std::memcpy(dst, c->buffer, static_cast<size_t>(bs) * sizeof(float));
        } else {
            std::memset(dst, 0, static_cast<size_t>(bs) * sizeof(float));
        }
        ++inIndex;
    }
    mainManager->inNode->input = innerInWfCh > 0 ? inputPatchBuffer.data() : nullptr;
    mainManager->inNode->numChannels = innerInWfCh;

    // Event inputs: copy parent patcher event inputs into inner InputNode event outputs.
    std::vector<Connection*> patcherEventInputs;
    for (auto* c : inputs.connections) {
        if (c && c->type == DataType::Events) patcherEventInputs.push_back(c);
    }
    std::vector<Connection*> innerEventOutputs;
    for (auto* c : mainManager->inNode->outputs.connections) {
        if (c && c->type == DataType::Events) innerEventOutputs.push_back(c);
    }
    const size_t ecount = std::min(patcherEventInputs.size(), innerEventOutputs.size());
    for (auto* c : innerEventOutputs) {
        if (c && c->events) c->events->clear();
    }
    for (size_t i = 0; i < ecount; ++i) {
        auto* src = patcherEventInputs[i];
        auto* dst = innerEventOutputs[i];
        if (!dst || !dst->events) continue;
        if (src && src->is_connected && src->events) {
            *dst->events = *src->events;
        }
    }

    patchBuffer.resize(static_cast<size_t>(innerWfCh) * static_cast<size_t>(bs), 0.0f);
    mainManager->process(patchBuffer.data(), bs, innerWfCh, sr);

    int wfIndex = 0;
    for (auto* out : outputs.connections) {
        if (!out || out->type != DataType::Waveform) continue;
        if (!out->buffer) continue;
        if (wfIndex < innerWfCh) {
            std::memcpy(out->buffer, patchBuffer.data() + static_cast<size_t>(wfIndex) * static_cast<size_t>(bs),
                        static_cast<size_t>(bs) * sizeof(float));
        } else {
            std::memset(out->buffer, 0, static_cast<size_t>(bs) * sizeof(float));
        }
        ++wfIndex;
    }

    // Event outputs: copy from inner OutputNode event inputs (same index order).
    std::vector<Connection*> innerEvIn;
    for (auto* c : mainManager->outNode->inputs.connections) {
        if (c && c->type == DataType::Events) innerEvIn.push_back(c);
    }
    const size_t wfOut = leadingWaveformOutputCount();
    size_t evPair = 0;
    for (size_t i = wfOut; i < outputs.connections.size(); ++i) {
        Connection* outc = outputs.connections[i];
        if (!outc || outc->type != DataType::Events || !outc->events) continue;
        outc->events->clear();
        if (evPair < innerEvIn.size()) {
            Connection* inc = innerEvIn[evPair];
            if (inc && inc->is_connected && inc->events) {
                *outc->events = *inc->events;
            }
        }
        ++evPair;
    }
}

void PatcherNode::renderContent(SDL_Renderer* renderer) {
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];
        vx[0] = 0;
        vx[1] = TEX_W;
        vx[2] = TEX_W;
        vx[3] = 0;
        vy[0] = 0;
        vy[1] = 0;
        vy[2] = TEX_H;
        vy[3] = TEX_H;
    }

    if (ne) {
        mainEditor->mouseX = (ne->mouseX - dstRect.x) / zoomRatio;
        mainEditor->mouseY = (ne->mouseY - dstRect.y) / zoomRatio;
    }
    mainEditor->tick(renderer);

    renderParams(renderer);
}

bool PatcherNode::handleCustomInput(SDL_Event& e) {
    if (mainEditor) {
        mainEditor->mouseX = msX;
        mainEditor->mouseY = msY;
        mainEditor->handleInput(e);
    }
    return true;
}

void PatcherNode::clearCustomTextures() {
    if (neTex) SDL_DestroyTexture(neTex);
    neTex = nullptr;
}

json PatcherNode::extraSerialize() {
    json j;
    j["mainManager"] = mainManager->serialize();
    j["outputs"] = json::array();
    for (auto* c : outputs.connections) {
        json jc;
        jc["id"] = c->id;
        jc["type"] = c->type;
        j["outputs"].push_back(jc);
    }
    std::cout << "[DBG_DESER] PatcherNode::extraSerialize node=" << id
              << " outputs=" << outputs.connections.size() << std::endl;
    return j;
}

void PatcherNode::extraDeSerialize(json j) {
    if (!j.contains("mainManager")) return;
    std::cout << "[DBG_DESER] PatcherNode::extraDeSerialize node=" << id << " begin" << std::endl;
    mainManager->deSerialize(j["mainManager"]);

    if (j.contains("outputs")) {
        for (auto* c : outputs.connections) {
            if (c->type == DataType::Waveform && c->buffer) {
                delete[] c->buffer;
                c->buffer = nullptr;
            } else if (c->type == DataType::Events && c->events) {
                delete c->events;
                c->events = nullptr;
            }
            delete c;
        }
        outputs.connections.clear();
        outputs.ids.clear();
        outputs.id_pool = idManager();

        for (auto jc : j["outputs"]) {
            auto* c = new Connection;
            c->nm = outputs.nm;
            c->id = jc["id"];
            c->type = jc["type"];
            c->dir = Direction::output;
            c->is_connected = false;
            c->input_node = id;
            c->input_connection = c->id;
            c->output_node = -1;
            c->output_connection = -1;
            if (c->type == DataType::Events) {
                c->events = new std::vector<Event>;
                c->buffer = nullptr;
            } else {
                c->buffer = nullptr;
                c->bufferSize = 0;
                c->events = nullptr;
            }
            outputs.connections.push_back(c);
            outputs.ids[c->id] = outputs.connections.size() - 1;
            outputs.id_pool.reserveID(c->id);
        }
        makeConnectionRects();
        std::cout << "[DBG_DESER]  patcher outputs restored count=" << outputs.connections.size() << std::endl;
    }
    setLinkedWaveformChannelCount(mainManager->outNode->countWaveformInputs());
    {
        size_t evSockets = 0;
        for (auto* c : mainManager->outNode->inputs.connections) {
            if (c && c->type == DataType::Events) ++evSockets;
        }
        setLinkedEventOutputCount(evSockets);
    }
    setLinkedWaveformInputCount(mainManager->inNode->countWaveformOutputs());
    {
        size_t evSockets = 0;
        for (auto* c : mainManager->inNode->outputs.connections) {
            if (c && c->type == DataType::Events) ++evSockets;
        }
        setLinkedEventInputCount(evSockets);
    }
    nm->markTopologyDirty();
    std::cout << "[DBG_DESER] PatcherNode::extraDeSerialize node=" << id << " end" << std::endl;
}
