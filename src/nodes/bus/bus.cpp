#include "bus.h"
#include "NodeManager.h"
#include <cstring>
#include <algorithm>

BusNode::BusNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Bus) {
    name = "Bus";

    for (int i = 0; i < BUS_NUM_INPUTS; i++) addInput();

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    out->minChannels = 1;
    out->updateNumChannels();
    outputs.addConnection(out);
}

void BusNode::addInput() {
    auto in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);
}

int BusNode::maxSourceChannels() const {
    int maxCh = 1;
    if (!nm) return maxCh;

    for (auto* c : inputs.connections) {
        if (!c->is_connected || c->type != DataType::Waveform) continue;
        Node* sn = nm->getNode(c->input_node);
        if (!sn) continue;
        // Channel count comes from the source's output socket, not our input.
        Connection* sc = sn->outputs.getConnection(c->input_connection);
        if (sc && sc->numChannels > maxCh) maxCh = sc->numChannels;
    }
    return maxCh;
}

void BusNode::setup() {
    if (!out) return;

    // Size the output from the widest connected source. Node::update() calls
    // this on every topology change, after the generic buffer pass — so the
    // allocation is settled here rather than mid-block in process().
    out->minChannels = maxSourceChannels();
    out->updateNumChannels(); // a wider downstream requirement still wins

    if (bufferSize > 0 && (out->buffer == nullptr || out->allocChannels != out->numChannels))
        out->allocateBuffer(bufferSize, out->numChannels);
}

void BusNode::process() {
    if (!out->is_connected || !out->buffer) return;

    const int outCh = out->numChannels;
    if (outCh <= 0) return;

    // Zero & sum. Output width is fixed in setup(); never grown here.
    std::memset(out->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(outCh) * sizeof(float));

    for (auto* c : inputs.connections) {
        if (!c->is_connected || c->type != DataType::Waveform || !c->buffer) continue;

        int srcCh = 1;
        if (nm) {
            Node* sn = nm->getNode(c->input_node);
            if (sn) {
                Connection* sc = sn->outputs.getConnection(c->input_connection);
                if (sc) srcCh = sc->numChannels;
            }
        }

        const int nch = std::min(srcCh, outCh);
        for (int ch = 0; ch < nch; ++ch) {
            float* outChBuf = out->channel(ch);
            float* inChBuf = c->buffer + static_cast<size_t>(ch) * static_cast<size_t>(bufferSize);
            for (int i = 0; i < bufferSize; ++i)
                outChBuf[i] += inChBuf[i];
        }
    }
}
