#include "patcher.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include "OutputNode.h"
#include <cstring>
#include <iostream>
#include <vector>

PatcherNode::PatcherNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Patcher) {
    auto path = nm->managerPath;
    path.push_back(id);
    mainManager = new NodeManager(project, path);
    mainManager->outNode->setCoupledPatcher(this);
}

PatcherNode::~PatcherNode() {
    attachFinal();
    if (mainManager) {
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
        removeLastLinkedWaveformFromBlock();
    }
    while (leadingWaveformOutputCount() < count) {
        insertLinkedWaveformAtEndOfBlock();
    }
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
        removeLastTrailingEventOutput();
    }
    while (trailingEventOutputCount() < count) {
        appendEventOutput();
    }
}

void PatcherNode::process() {
    int bs = bufferSize;
    int sr = sampleRate;
    if (bs <= 0) return;

    int innerWfCh = static_cast<int>(mainManager->outNode->countWaveformInputs());
    if (innerWfCh < 1) innerWfCh = 1;

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
            vx[2] = TEX_W - 300;
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
            c->events = nullptr;
            c->buffer = nullptr;
            c->bufferSize = 0;
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
    nm->markTopologyDirty();
    std::cout << "[DBG_DESER] PatcherNode::extraDeSerialize node=" << id << " end" << std::endl;
}
