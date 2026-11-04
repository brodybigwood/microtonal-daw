#include "mixer.h"
#include "NodeManager.h"
#include "NodeProcessor.h"
#include "UndoManager.h"
#include "ContextMenu.h"
#include "TreeEntry.h"
#include "styles.h"
#include "nodes/nodetypes.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

// ============================================================================
// Helpers
// ============================================================================

static Connection* firstWaveIn(Node* n) {
    for (auto* c : n->inputs.connections)
        if (c->type == DataType::Waveform) return c;
    return nullptr;
}

static Connection* firstWaveOut(Node* n) {
    for (auto* c : n->outputs.connections)
        if (c->type == DataType::Waveform) return c;
    return nullptr;
}

static float columnLeft(size_t i, float scroll) {
    return MixerNode::kColPad + static_cast<float>(i) * (MixerNode::kColW + MixerNode::kColPad) - scroll;
}

static bool inRect(float x, float y, const SDL_FRect& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void drawText(SDL_Renderer* R, float x, float y, const std::string& s, SDL_Color col, int scale = 1) {
    if (!fonts.mainFont) return;
    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, s.c_str(), static_cast<size_t>(s.size()), col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(R, surf);
    if (tex) {
        SDL_FRect dst{x, y, static_cast<float>(surf->w * scale), static_cast<float>(surf->h * scale)};
        SDL_RenderTexture(R, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_DestroySurface(surf);
}

static std::string typeName(const Node* n) {
    const int t = static_cast<int>(n->nodeType);
    return (t >= 0 && t < static_cast<int>(NodeType::Count)) ? NodeTypeStr[t] : std::string("?");
}

// ============================================================================
// MixerNode::Chain
// ============================================================================

MixerNode::Chain::Chain()
    : pan(0.5f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, "Pan"),
      volume(1.0f, 0.f, 0.f, 8.f, 40.f) {
    pan.displayLabel = "Pan";
}

// ============================================================================
// Construction
// ============================================================================

MixerNode::MixerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Mixer) {
    name = "Mixer";

    vCount = 4;
    vx = new float[4]{0.f, NODE_W, NODE_W, 0.f};
    vy = new float[4]{0.f, 0.f, kNODE_H, kNODE_H};
    dstRect.h = kNODE_H;
    w = NODE_W;
    h = kNODE_H;

    // One multichannel input per chain; each is routed into that chain's bottom node.
    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    out->minChannels = 1;
    out->updateNumChannels();
    outputs.addConnection(out);

    // Fresh node: create chains directly; there is nothing to mirror or undo yet.
    addChainOnGraph(static_cast<int>(chainIdPool_.newID()), 0);
    addChainOnGraph(static_cast<int>(chainIdPool_.newID()), 0);
    addChainOnGraph(static_cast<int>(chainIdPool_.newID()), 0);
    addChainOnGraph(static_cast<int>(chainIdPool_.newID()), 0);
}

MixerNode::~MixerNode() {
    closeChainWindow();
    for (auto& cp : chains)
        for (Node* n : cp->nodes) destroyNode(n);
    chains.clear();
    if (vx) delete[] vx;
    if (vy) delete[] vy;
    vx = nullptr;
    vy = nullptr;
}

Connection* MixerNode::addInputPort(uint16_t forcedID) {
    auto* c = new Connection;
    c->type = DataType::Waveform;
    c->dir = Direction::input;
    if (forcedID == 0) {
        inputs.addConnection(c); // allocates the id
        return c;
    }
    c->nm = inputs.nm;
    c->id = forcedID;
    c->output_connection = c->id;
    c->output_node = inputs.nodeID;
    c->input_connection = -1;
    c->input_node = -1;
    c->events = nullptr;
    c->buffer = nullptr;
    c->bufferSize = 0;
    c->allocChannels = c->numChannels;
    inputs.connections.push_back(c);
    inputs.id_pool.reserveID(c->id);
    inputs.ids[c->id] = static_cast<uint16_t>(inputs.connections.size() - 1);
    return c;
}

void MixerNode::removeInputPort(Connection* c) {
    if (!c) return;
    for (size_t i = 0; i < inputs.connections.size(); ++i) {
        if (inputs.connections[i] != c) continue;
        inputs.connections.erase(inputs.connections.begin() + static_cast<ptrdiff_t>(i));
        break;
    }
    inputs.id_pool.releaseID(c->id);
    inputs.ids.erase(c->id);
    delete c;
}

void MixerNode::addChain(uint16_t inputPortID) {
    const int cid = static_cast<int>(chainIdPool_.newID());
    if (!project || !project->um || !nm) {
        addChainOnGraph(cid, inputPortID);
        return;
    }
    const uint16_t pid = inputPortID != 0 ? inputPortID : inputs.id_pool.newID();
    project->um->newAction(new MixerChainEditUndoAction(project, nm->managerPath,
        static_cast<int>(id), MixerChainEditUndoAction::Add, cid, pid, {}, json(),
        "Add Mixer Chain"));
}

void MixerNode::removeChain(int index) {
    if (index < 0 || index >= static_cast<int>(chains.size())) return;
    const int cid = chains[static_cast<size_t>(index)]->id;
    if (!project || !project->um || !nm) {
        removeChainOnGraph(cid);
        return;
    }
    project->um->newAction(new MixerChainEditUndoAction(project, nm->managerPath,
        static_cast<int>(id), MixerChainEditUndoAction::Remove, cid, 0, {}, chainJson(cid),
        "Remove Mixer Chain"));
}

MixerNode::Chain* MixerNode::chainByID(int chainID) {
    for (auto& c : chains)
        if (c->id == chainID) return c.get();
    return nullptr;
}

Node* MixerNode::createNode(NodeType type) {
    const uint16_t nid = nm ? nm->allocateNodeID()
                            : static_cast<uint16_t>(nodeIdPool_.newID());
    Node* n = byType(type, static_cast<int>(nid), nm);
    if (!n) return nullptr;
    if (nm) nm->registerExtraNode(n);
    n->update(bufferSize, sampleRate);
    n->setup();
    return n;
}

// Chain inserts share the owning manager's id namespace, so actions can resolve them
// through the normal managerPath + nodeID lookup. Saved chains may carry ids from the
// old per-chain pool; renumber any that collide with a graph node or system node.
static uint16_t resolveChainNodeID(NodeManager* nm, const json& jn, json& out) {
    out = jn;
    const uint16_t want = static_cast<uint16_t>(jn.value("id", 0));
    if (!nm) return want;
    if (want > 1 && !nm->getNode(want)) {
        nm->reserveNodeID(want);
        return want;
    }
    const uint16_t id = nm->allocateNodeID();
    out["id"] = id;
    return id;
}

static void stampChainOwnership(Node* n, uint16_t mixerID, int chainID) {
    if (!n) return;
    n->mixerOwnerNodeID = static_cast<int>(mixerID);
    n->mixerOwnerChainID = chainID;
    // Nested graphs (Delay feedback patcher) need an address that resolves through the
    // mixer chain: [mixerID, chainID, insertID].
    if (auto* d = dynamic_cast<DelayNode*>(n); d && d->chain) {
        std::vector<int> path = n->nm ? n->nm->managerPath : std::vector<int>{};
        path.push_back(static_cast<int>(mixerID));
        path.push_back(chainID);
        path.push_back(static_cast<int>(n->id));
        d->chain->managerPath = std::move(path);
    }
}

Node* MixerNode::chainNodeByID(int chainID, int nodeID) {
    Chain* c = chainByID(chainID);
    if (!c) return nullptr;
    for (Node* n : c->nodes)
        if (n && static_cast<int>(n->id) == nodeID) return n;
    return nullptr;
}

Node* MixerNode::insertChainNodeFromSnapshot(int chainID, const json& nodeSnapshot) {
    Chain* c = chainByID(chainID);
    if (!c || !nm) return nullptr;
    g_vstLoadingChainInsert = true;
    Node* n = Node::deSerialize(nodeSnapshot, nm, false);
    g_vstLoadingChainInsert = false;
    if (!n) return nullptr;
    nm->reserveNodeID(n->id);
    nm->registerExtraNode(n);
    stampChainOwnership(n, id, chainID);
    c->nodes.push_back(n);
    c->appliedInCh = -1;
    nm->markTopologyDirty();
    return n;
}

bool MixerNode::removeChainNodeByID(int chainID, int nodeID) {
    Chain* c = chainByID(chainID);
    if (!c) return false;
    for (size_t i = 0; i < c->nodes.size(); ++i) {
        Node* n = c->nodes[i];
        if (!n || static_cast<int>(n->id) != nodeID) continue;
        destroyNode(n);
        c->nodes.erase(c->nodes.begin() + static_cast<ptrdiff_t>(i));
        c->appliedInCh = -1;
        if (nm) nm->markTopologyDirty();
        return true;
    }
    return false;
}

void MixerNode::destroyNode(Node* n) {
    if (!n) return;
    if (nm) nm->unregisterExtraNode(n);
    delete n;
}

json MixerNode::chainNodesJson(int chainID) const {
    json arr = json::array();
    for (const auto& cp : chains) {
        if (cp->id != chainID) continue;
        for (Node* n : cp->nodes)
            if (n) arr.push_back(n->serialize());
        break;
    }
    return arr;
}

void MixerNode::applyChainNodes(int chainID, const json& nodesJson) {
    Chain* c = chainByID(chainID);
    if (!c) return;
    for (Node* n : c->nodes) destroyNode(n);
    c->nodes.clear();
    if (nodesJson.is_array()) {
        for (const auto& jn : nodesJson) {
            json jn2;
            resolveChainNodeID(nm, jn, jn2);
            g_vstLoadingChainInsert = true;
            Node* n = Node::deSerialize(jn2, nm, false);
            g_vstLoadingChainInsert = false;
            if (!n) continue;
            nodeIdPool_.reserveID(n->id);
            if (nm) nm->registerExtraNode(n);
            stampChainOwnership(n, id, chainID);
            c->nodes.push_back(n);
        }
    }
    // Processing order is the vector order; no explicit wiring to rebuild.
    c->appliedInCh = -1;
    if (nm) nm->markTopologyDirty();
}

int MixerNode::addNodeToChain(int chainIndex, NodeType type) {
    if (chainIndex < 0 || chainIndex >= static_cast<int>(chains.size())) return -1;
    Chain* c = chains[chainIndex].get();
    Node* temp = createNode(type);
    if (!temp) return -1;
    const int addedID = static_cast<int>(temp->id);
    json nodeSnapshot = temp->serialize();
    destroyNode(temp);  // the action re-creates it from the snapshot

    if (!project || !project->um || !nm) {
        return insertChainNodeFromSnapshot(c->id, nodeSnapshot) ? addedID : -1;
    }
    project->um->newAction(new MixerChainInsertNodeAction(project, nm->managerPath,
        static_cast<int>(id), c->id, std::move(nodeSnapshot)));
    return addedID;
}

void MixerNode::removeNodeFromChain(int chainIndex, int nodeIndex) {
    if (chainIndex < 0 || chainIndex >= static_cast<int>(chains.size())) return;
    Chain* c = chains[chainIndex].get();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(c->nodes.size())) return;
    Node* n = c->nodes[static_cast<size_t>(nodeIndex)];
    if (!n) return;
    json nodeSnapshot = n->serialize();
    const int nid = static_cast<int>(n->id);
    if (project && project->um && nm) {
        project->um->newAction(new MixerChainInsertNodeAction(project, nm->managerPath,
            static_cast<int>(id), c->id, std::move(nodeSnapshot), /*removeOnDo=*/true,
            "Mixer Remove Node"));
    } else {
        removeChainNodeByID(c->id, nid);
    }
}

void MixerNode::syncChainParams() {
    // Per-chain pan/volume live in the node's parameter list (pan at 2*i,
    // volume at 2*i+1) so SetParamValueUndoAction can drive them.
    params.clear();
    params.reserve(chains.size() * 2);
    for (size_t i = 0; i < chains.size(); ++i) {
        auto& cp = chains[i];
        cp->volume.bind(this, i * 2 + 1);
        params.push_back(&cp->pan);
        params.push_back(&cp->volume);
    }
}

void MixerNode::addChainOnGraph(int chainID, uint16_t portID) {
    if (chainByID(chainID)) return;
    auto c = std::make_unique<Chain>();
    c->id = chainID;
    chainIdPool_.reserveID(static_cast<uint16_t>(chainID));
    c->inflow = addInputPort(portID);
    chains.push_back(std::move(c));
    syncChainParams();
    if (nm) nm->markTopologyDirty();
}

void MixerNode::removeChainOnGraph(int chainID) {
    for (size_t i = 0; i < chains.size(); ++i) {
        if (chains[i]->id != chainID) continue;
        for (Node* n : chains[i]->nodes) destroyNode(n);
        removeInputPort(chains[i]->inflow);
        chains.erase(chains.begin() + static_cast<ptrdiff_t>(i));
        syncChainParams();
        if (nm) nm->markTopologyDirty();
        return;
    }
}

void MixerNode::reorderChainsOnGraph(const std::vector<int>& chainIDs) {
    if (chainIDs.size() != chains.size()) return;

    // Resolve every id to an index first, so a missing id cannot leave the
    // vector partially moved/erased.
    std::vector<size_t> order;
    order.reserve(chainIDs.size());
    std::vector<bool> used(chains.size(), false);
    for (int cid : chainIDs) {
        size_t found = SIZE_MAX;
        for (size_t i = 0; i < chains.size(); ++i) {
            if (!used[i] && chains[i] && chains[i]->id == cid) { found = i; break; }
        }
        if (found == SIZE_MAX) return;
        used[found] = true;
        order.push_back(found);
    }

    std::vector<std::unique_ptr<Chain>> reordered;
    reordered.reserve(chains.size());
    for (size_t idx : order) reordered.push_back(std::move(chains[idx]));
    chains = std::move(reordered); // pointers only

    // Keep the input-port row in the same order as the chain columns. Port
    // objects and ids are unchanged, so existing cables stay valid.
    std::vector<Connection*> newInputs;
    newInputs.reserve(inputs.connections.size());
    for (auto& cp : chains)
        if (cp->inflow) newInputs.push_back(cp->inflow);
    for (Connection* c : inputs.connections) {
        if (std::find(newInputs.begin(), newInputs.end(), c) != newInputs.end()) continue;
        newInputs.push_back(c); // e.g. mapped-parameter ports
    }
    inputs.connections = std::move(newInputs);
    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);

    syncChainParams();
    makeConnectionRects();
    if (nm) nm->markTopologyDirty();
}

json MixerNode::chainJson(int chainID) const {
    json jc;
    for (const auto& cp : chains) {
        if (cp->id != chainID) continue;
        const Chain& c = *cp;
        jc["id"] = c.id;
        if (c.inflow) jc["inputConnectionID"] = c.inflow->id;
        jc["pan"] = c.pan.value;
        jc["vol"] = c.volume.value;
        jc["solo"] = c.solo;
        jc["mute"] = c.mute;
        jc["nodes"] = json::array();
        for (Node* n : c.nodes)
            if (n) jc["nodes"].push_back(n->serialize());
        break;
    }
    return jc;
}

void MixerNode::restoreChain(const json& jc) {
    const int cid = jc.value("id", -1);
    if (cid < 0 || chainByID(cid)) return;
    auto c = std::make_unique<Chain>();
    c->id = cid;
    chainIdPool_.reserveID(static_cast<uint16_t>(cid));
    c->inflow = addInputPort(static_cast<uint16_t>(jc.value("inputConnectionID", 0)));
    c->pan.value = jc.value("pan", 0.5f);
    c->volume.value = jc.value("vol", 1.0f);
    c->solo = jc.value("solo", false);
    c->mute = jc.value("mute", false);
    if (jc.contains("nodes") && jc["nodes"].is_array()) {
        for (const auto& jn : jc["nodes"]) {
            json jn2;
            resolveChainNodeID(nm, jn, jn2);
            g_vstLoadingChainInsert = true;
            Node* n = Node::deSerialize(jn2, nm, false);
            g_vstLoadingChainInsert = false;
            if (!n) continue;
            nodeIdPool_.reserveID(n->id);
            if (nm) nm->registerExtraNode(n);
            stampChainOwnership(n, id, cid);
            c->nodes.push_back(n);
        }
    }
    chains.push_back(std::move(c));
    syncChainParams();
    if (nm) nm->markTopologyDirty();
}

void MixerNode::reorderChains(const std::vector<size_t>& newOrder) {
    if (newOrder.size() != chains.size()) return;
    if (!project || !project->um || !nm) {
        std::vector<int> ids;
        for (size_t idx : newOrder) ids.push_back(chains[idx]->id);
        reorderChainsOnGraph(ids);
        return;
    }
    std::vector<int> ids;
    for (size_t idx : newOrder) ids.push_back(chains[idx]->id);
    project->um->newAction(new MixerChainEditUndoAction(project, nm->managerPath,
        static_cast<int>(id), MixerChainEditUndoAction::Reorder, 0, 0, std::move(ids), json(),
        "Reorder Mixer Chains"));
}

// ============================================================================
// Audio
// ============================================================================

void MixerNode::setup() {
    if (!out) return;
    out->minChannels = std::max(1, outChannelsTarget_);
    out->updateNumChannels();
    if (bufferSize > 0 && (out->buffer == nullptr || out->allocChannels != out->numChannels))
        out->allocateBuffer(bufferSize, out->numChannels);
}

void MixerNode::process() {
    if (!out || !out->buffer || bufferSize <= 0) return;

    const int bs = bufferSize;
    const int sr = sampleRate > 0 ? sampleRate : 48000;

    if (static_cast<int>(silence_.size()) < bs) silence_.assign(static_cast<size_t>(bs), 0.0f);

    int maxCh = 1;
    int maxLat = 0;

    for (auto& cp : chains) {
        Chain& c = *cp;
        // Each chain is fed by its own multichannel input port, channel order preserved.
        // Use the cable's actual source width, not this port's declared width, so a
        // stereo source is not collapsed to mono here.
        const bool haveIn = c.inflow && c.inflow->is_connected && c.inflow->buffer;
        const int srcWidth = c.inflow ? std::max(c.inflow->sourceChannels, c.inflow->numChannels) : 0;
        const int inCh = haveIn ? std::max(1, srcWidth) : 0;
        float* inData = haveIn ? c.inflow->channel(0) : silence_.data();

        int ch = std::max(1, inCh);
        float* signal = inData;

        // Prepare: push the channel order through the serial list, bottom -> top.
        // A node's input is the node below it; the bottom node's input is the
        // mixer's exposed connection for this chain.
        if (c.appliedInCh != inCh) {
            int prepCh = std::max(1, inCh);
            for (int i = static_cast<int>(c.nodes.size()) - 1; i >= 0; --i) {
                Node* n = c.nodes[static_cast<size_t>(i)];
                Connection* din = firstWaveIn(n);
                Connection* dout = firstWaveOut(n);
                if (!din || !dout) break;
                din->is_connected = true;
                din->numChannels = prepCh;
                din->sourceChannels = prepCh;
                din->bufferSize = bs;
                dout->minChannels = prepCh;
                dout->updateNumChannels();
                n->update(bs, sr);
                prepCh = std::max(1, static_cast<int>(dout->numChannels));
            }
            c.appliedInCh = inCh;
        }

        int latency = 0;
        for (int i = static_cast<int>(c.nodes.size()) - 1; i >= 0; --i) {
            Node* n = c.nodes[static_cast<size_t>(i)];
            Connection* din = firstWaveIn(n);
            Connection* dout = firstWaveOut(n);
            if (!din || !dout) { signal = silence_.data(); ch = 1; break; }
            din->is_connected = true;
            din->buffer = signal;
            din->bufferSize = bs;
            din->numChannels = ch;
            din->sourceChannels = ch;
            n->process();
            if (dout->buffer) {
                signal = dout->buffer;
                ch = std::max(1, static_cast<int>(dout->numChannels));
            } else {
                // No output buffer (e.g. a node whose ports were rebuilt without an
                // update): the only valid fallback is a single-channel silence block.
                signal = silence_.data();
                ch = 1;
            }
            latency += std::max(0, n->reportedLatency());
        }

        c.channels = ch;
        c.latency = latency;
        c.buf.assign(static_cast<size_t>(ch) * static_cast<size_t>(bs), 0.0f);
        if (signal)
            std::memcpy(c.buf.data(), signal,
                        static_cast<size_t>(ch) * static_cast<size_t>(bs) * sizeof(float));

        maxCh = std::max(maxCh, ch);
        maxLat = std::max(maxLat, latency);
    }

    if (maxLat != cachedLatency_) {
        cachedLatency_ = maxLat;
        if (nm) nm->markTopologyDirty(); // downstream PDC must account for it
    }
    if (maxCh != outChannelsTarget_) {
        outChannelsTarget_ = maxCh;
        if (nm) nm->markTopologyDirty();
    }

    const int outCh = std::max(1, std::min(out->numChannels, 32));
    for (int cc = 0; cc < outCh; ++cc)
        std::memset(out->channel(cc), 0, static_cast<size_t>(bs) * sizeof(float));

    bool anySolo = false;
    for (auto& cp : chains) {
        if (cp->solo && !cp->mute) { anySolo = true; break; }
    }

    for (auto& cp : chains) {
        Chain& c = *cp;
        if (c.channels <= 0 || c.mute) continue;
        if (anySolo && !c.solo) continue;
        const int d = std::max(0, maxLat - c.latency);
        if (d != c.delay || static_cast<int>(c.ring.size()) != c.channels) {
            c.ring.assign(static_cast<size_t>(c.channels),
                          std::vector<float>(static_cast<size_t>(std::max(1, d)), 0.0f));
            c.writePos = 0;
            c.delay = d;
        }
        const float panL = std::cos(c.pan.value * static_cast<float>(M_PI_2));
        const float panR = std::sin(c.pan.value * static_cast<float>(M_PI_2));
        const float vol = c.volume.value;
        const int nch = std::min(c.channels, outCh);
        for (int cc = 0; cc < nch; ++cc) {
            const float pg = (nch >= 2) ? ((cc == 0) ? panL : panR) : 1.f;
            const float gain = vol * pg;
            const float* src = c.buf.data() + static_cast<size_t>(cc) * bs;
            float* dst = out->channel(cc);
            if (d <= 0) {
                for (int i = 0; i < bs; ++i) dst[i] += src[i] * gain;
                continue;
            }
            std::vector<float>& ring = c.ring[static_cast<size_t>(cc)];
            for (int i = 0; i < bs; ++i) {
                const int idx = (c.writePos + i) % d;
                dst[i] += ring[static_cast<size_t>(idx)] * gain;
                ring[static_cast<size_t>(idx)] = src[i];
            }
        }
        if (d > 0) c.writePos = (c.writePos + bs) % d;
    }
}

// ============================================================================
// Content rendering (same column layout as the sampler, pan knob in place of
// the waveform thumbnail; each column is one chain).
// ============================================================================

void MixerNode::render(SDL_Renderer* R) {
    if (!R) return;
    if (!texture || !dstRect.h) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET, static_cast<int>(NODE_W), static_cast<int>(kNODE_H));
    }
    Node::render(R);
}

void MixerNode::renderContent(SDL_Renderer* R) {
    SDL_SetRenderDrawColor(R, 35, 35, 40, 255);
    SDL_FRect bg{0, 0, NODE_W, kNODE_H};
    SDL_RenderFillRect(R, &bg);

    const float listTop = kListTop + 24.f;
    const float colContentH = kPanH + kRowH + 2.f * kBtnSz + 2.f + kSliderH + 4.f;

    for (size_t i = 0; i < chains.size(); ++i) {
        auto& c = *chains[i];
        const float cx = columnLeft(i, scrollOffset_);
        if (cx + kColW < 0.f || cx > NODE_W) continue;

        SDL_FRect colBg{cx, listTop, kColW, colContentH};
        SDL_SetRenderDrawColor(R, 50, 50, 55, 255);
        SDL_RenderFillRect(R, &colBg);
        if (hoveredChainIndex_ == static_cast<int>(i))
            SDL_SetRenderDrawColor(R, 140, 180, 220, 255);
        else
            SDL_SetRenderDrawColor(R, 80, 80, 85, 255);
        SDL_RenderRect(R, &colBg);

        float cy = listTop + 2.f;

        // Pan knob replaces the old sample view.
        c.pan.reposition(cx + kColW * 0.5f, cy + kPanH * 0.5f, 8.f);
        c.pan.render(R);
        cy += kPanH;

        // Chain label.
        const std::string label = "T" + std::to_string(c.id);
        drawText(R, cx + 2.f, cy, label, SDL_Color{200, 200, 200, 255});
        cy += kRowH;

        // Solo [S] / Mute [M].
        const float btnX = cx + (kColW - kBtnSz) * 0.5f;
        SDL_FRect soloR{btnX, cy, kBtnSz, kBtnSz};
        SDL_FRect muteR{btnX, cy + kBtnSz + 2.f, kBtnSz, kBtnSz};
        SDL_Color soloCol = c.solo ? SDL_Color{255, 200, 50, 255} : SDL_Color{70, 70, 75, 255};
        SDL_Color muteCol = c.mute ? SDL_Color{255, 60, 60, 255} : SDL_Color{70, 70, 75, 255};
        SDL_SetRenderDrawColor(R, soloCol.r, soloCol.g, soloCol.b, soloCol.a);
        SDL_RenderFillRect(R, &soloR);
        SDL_SetRenderDrawColor(R, muteCol.r, muteCol.g, muteCol.b, muteCol.a);
        SDL_RenderFillRect(R, &muteR);
        drawText(R, soloR.x + 3.f, soloR.y, "S", SDL_Color{235, 235, 235, 255});
        drawText(R, muteR.x + 3.f, muteR.y, "M", SDL_Color{235, 235, 235, 255});
        cy += 2.f * kBtnSz + 4.f;

        // Volume bar owns its own input and undo.
        const float sliderW = 10.f;
        const float sliderX = cx + (kColW - sliderW) * 0.5f;
        c.volume.reposition(sliderX, cy, sliderW, kSliderH);
        c.volume.render(R);
    }

    // Drop indicator: where the dragged chain column will land in the order.
    if (draggingChain_ >= 0 && dragTargetInsert_ >= 0 &&
        dragTargetInsert_ <= static_cast<int>(chains.size())) {
        const float ix = columnLeft(static_cast<size_t>(dragTargetInsert_), scrollOffset_) - kColPad * 0.5f;
        SDL_SetRenderDrawColor(R, 255, 200, 50, 255);
        SDL_FRect dropLine{ix - 1.f, listTop, 2.f, colContentH};
        SDL_RenderFillRect(R, &dropLine);
    }

    // Add button.
    SDL_FRect addBtn{NODE_W / 2.f - 40.f, kNODE_H - kFooterH - 4.f, 80.f, kFooterH};
    SDL_SetRenderDrawColor(R, 60, 100, 160, 255);
    SDL_RenderFillRect(R, &addBtn);
    drawText(R, addBtn.x + 14.f, addBtn.y + 4.f, "+ Add", SDL_Color{255, 255, 255, 255});
}

// ============================================================================
// Input
// ============================================================================

bool MixerNode::handleCustomInput(SDL_Event& e) {
    const float listTop = kListTop + 24.f;
    const float colContentH = kPanH + kRowH + 2.f * kBtnSz + 2.f + kSliderH + 4.f;

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const float mx = msX;
        const float my = msY;

        SDL_FRect addBtn{NODE_W / 2.f - 40.f, kNODE_H - kFooterH - 4.f, 80.f, kFooterH};
        if (e.button.button == SDL_BUTTON_LEFT && inRect(mx, my, addBtn)) {
            addChain();
            return true;
        }

        for (size_t i = 0; i < chains.size(); ++i) {
            auto& c = *chains[i];
            const float cx = columnLeft(i, scrollOffset_);
            // Both axes: a click above/below the node (e.g. on a port) must never be
            // interpreted as a column click.
            if (mx < cx || mx > cx + kColW) continue;
            if (my < listTop || my > listTop + colContentH) continue;
            float cy = listTop + 2.f;

            // Pan knob.
            SDL_FRect panR{cx + 2.f, cy, kColW - 4.f, kPanH - 4.f};
            if (inRect(mx, my, panR)) {
                const float oldVal = c.pan.value;
                c.pan.handleInput(e);
                if (std::fabs(c.pan.value - oldVal) > 0.0005f && project && project->um && nm) {
                    std::vector<size_t> pp{static_cast<size_t>(i) * 2};
                    project->um->newAction(new SetParamValueUndoAction(project, nm->managerPath,
                        static_cast<int>(id), std::move(pp), oldVal, c.pan.value, "Mixer Pan"));
                }
                return true;
            }
            cy += kPanH;
            cy += kRowH;

            const float btnX = cx + (kColW - kBtnSz) * 0.5f;
            SDL_FRect soloR{btnX, cy, kBtnSz, kBtnSz};
            SDL_FRect muteR{btnX, cy + kBtnSz + 2.f, kBtnSz, kBtnSz};
            if (e.button.button == SDL_BUTTON_LEFT && inRect(mx, my, soloR)) {
                if (project && project->um && nm)
                    project->um->newAction(new MixerChainEditUndoAction(project, nm->managerPath,
                        static_cast<int>(id), MixerChainEditUndoAction::Solo, c.id, 0, {}, json(),
                        "Mixer Solo"));
                else
                    c.solo = !c.solo;
                return true;
            }
            if (e.button.button == SDL_BUTTON_LEFT && inRect(mx, my, muteR)) {
                if (project && project->um && nm)
                    project->um->newAction(new MixerChainEditUndoAction(project, nm->managerPath,
                        static_cast<int>(id), MixerChainEditUndoAction::Mute, c.id, 0, {}, json(),
                        "Mixer Mute"));
                else
                    c.mute = !c.mute;
                return true;
            }
            cy += 2.f * kBtnSz + 4.f;

            const float sliderW = 10.f;
            const float sliderX = cx + (kColW - sliderW) * 0.5f;
            c.volume.reposition(sliderX, cy, sliderW, kSliderH);
            c.volume.handleInputAt(e, mx, my);
            if (c.volume.dragging()) {
                draggingVolumeIdx_ = static_cast<int>(i);
                return true;
            }

            if (e.button.button == SDL_BUTTON_RIGHT) {
                auto* ctx = ContextMenu::get();
                ctx->activate();
                auto root = uTreeEntry();
                auto del = uTreeEntry();
                del->label = "Remove Chain";
                const int ci = static_cast<int>(i);
                del->click = [this, ci]() { removeChain(ci); };
                root->addChild(del);
                ctx->dynamicTick = getTreeMenuTicker(root);
                return true;
            }

            if (e.button.button == SDL_BUTTON_LEFT) {
                draggingChain_ = static_cast<int>(i);
                dragStartX_ = mx;
                dragStartY_ = my;
                dragTargetInsert_ = static_cast<int>(i);
                return true;
            }
        }
        return false;
    }

    return false;
}

void MixerNode::handleWindowInput(SDL_Event& e) {
    const float mx = msX;
    const float my = msY;
    const float listTop = kListTop + 24.f;

    if (draggingChain_ >= 0) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            const int from = draggingChain_;
            int to = dragTargetInsert_;
            draggingChain_ = -1;
            dragTargetInsert_ = -1;
            if (to < 0) to = 0;
            if (to >= static_cast<int>(chains.size())) to = static_cast<int>(chains.size()) - 1;
            // Only a press-and-release without movement counts as a click. Otherwise a
            // drag that stayed inside its own column would open the chain window.
            const float dx = mx - dragStartX_;
            const float dy = my - dragStartY_;
            const bool moved = dx * dx + dy * dy > 9.f;
            if (from != to && from >= 0 && to >= 0 && static_cast<size_t>(to) < chains.size()) {
                std::vector<size_t> order(chains.size());
                std::iota(order.begin(), order.end(), static_cast<size_t>(0));
                order.erase(order.begin() + from);
                order.insert(order.begin() + to, static_cast<size_t>(from));
                reorderChains(order);
            } else if (!moved && from == to && from >= 0 && from < static_cast<int>(chains.size())) {
                openChainWindow(chains[static_cast<size_t>(from)]->id);
            }
            return;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            const float relX = mx + scrollOffset_ - kColPad;
            int target = static_cast<int>(relX / (kColW + kColPad));
            target = std::clamp(target, 0, static_cast<int>(chains.size()));
            dragTargetInsert_ = target;
            return;
        }
        return;
    }

    if (draggingVolumeIdx_ >= 0 && draggingVolumeIdx_ < static_cast<int>(chains.size())) {
        auto& c = *chains[static_cast<size_t>(draggingVolumeIdx_)];
        c.volume.handleInputAt(e, mx, my);
        if (!c.volume.dragging()) draggingVolumeIdx_ = -1;
        return;
    }

    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        hoveredChainIndex_ = -1;
        for (size_t i = 0; i < chains.size(); ++i) {
            const float cx = columnLeft(i, scrollOffset_);
            if (mx >= cx && mx <= cx + kColW && my >= listTop &&
                my <= listTop + kPanH + kRowH + 2.f * kBtnSz + 2.f + kSliderH + 4.f) {
                hoveredChainIndex_ = static_cast<int>(i);
                break;
            }
        }
    }

    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        // Wheel over a pan knob adjusts it; elsewhere it scrolls the strip.
        for (size_t i = 0; i < chains.size(); ++i) {
            const float cx = columnLeft(i, scrollOffset_);
            SDL_FRect panR{cx + 2.f, listTop + 2.f, kColW - 4.f, kPanH - 4.f};
            if (inRect(mx, my, panR)) {
                Chain& c = *chains[i];
                const float oldVal = c.pan.value;
                c.pan.handleInput(e);
                if (std::fabs(c.pan.value - oldVal) > 0.0005f && project && project->um && nm) {
                    std::vector<size_t> pp{static_cast<size_t>(i) * 2};
                    project->um->newAction(new SetParamValueUndoAction(project, nm->managerPath,
                        static_cast<int>(id), std::move(pp), oldVal, c.pan.value, "Mixer Pan"));
                }
                return;
            }
        }
        scrollOffset_ -= e.wheel.y * 30.f;
        const float totalW = static_cast<float>(chains.size()) * (kColW + kColPad) - kColPad;
        const float maxScroll = std::max(0.f, totalW - (NODE_W - kColPad * 2.f));
        scrollOffset_ = std::clamp(scrollOffset_, 0.f, maxScroll);
        return;
    }
}

// ============================================================================
// Chain window (insert list)
// ============================================================================

void MixerNode::openChainWindow(int chainID) {
    closeChainWindow();
    if (!project || !project->processor) return;
    auto* wm = project->processor->getWindowManager();
    if (!wm) return;

    auto win = std::make_unique<MixerChainWindow>(this, chainID);
    MixerChainWindow* ptr = win.get();
    ExpandedWindow* ew = wm->addWindow(std::move(win), 520, 380,
        ("Mixer Chain " + std::to_string(chainID)).c_str());
    if (!ew) return;
    ew->show();
    chainWindow_ = ptr;
}

void MixerNode::selectChainWindowInsert(int index) {
    if (chainWindow_)
        chainWindow_->setSelectedRow(index);
}

void MixerNode::closeChainWindow() {
    if (chainWindow_) {
        chainWindow_->onRequestClose();
        chainWindow_ = nullptr;
    }
}

MixerChainWindow::MixerChainWindow(MixerNode* node, int chainID)
    : mixerNode(node), chainID(chainID) {}

MixerChainWindow::~MixerChainWindow() {
    if (previewTex_) {
        SDL_DestroyTexture(previewTex_);
        previewTex_ = nullptr;
    }
    if (mixerNode)
        mixerNode->detachChainWindow(this);
}

void MixerNode::detachChainWindow(MixerChainWindow* w) {
    if (chainWindow_ == w)
        chainWindow_ = nullptr;
}

void MixerChainWindow::render() {
    SDL_Renderer* R = renderer;
    if (!R || !mixerNode) return;

    SDL_SetRenderDrawColor(R, 40, 40, 45, 255);
    SDL_RenderClear(R);

    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);

    drawText(R, 8.f, 6.f, "Chain " + std::to_string(chainID), SDL_Color{230, 230, 230, 255});

    MixerNode::Chain* c = mixerNode->chainByID(chainID);
    if (c && !c->nodes.empty())
        selectedRow_ = std::clamp(selectedRow_, 0, static_cast<int>(c->nodes.size()) - 1);
    else
        selectedRow_ = 0;

    const float rowH = 22.f;
    float y = 30.f;

    SDL_FRect list{6.f, y, 240.f, static_cast<float>(wh) - y - 40.f};
    SDL_SetRenderDrawColor(R, 30, 30, 34, 255);
    SDL_RenderFillRect(R, &list);
    SDL_SetRenderDrawColor(R, 90, 90, 100, 255);
    SDL_RenderRect(R, &list);

    if (c) {
        for (size_t i = 0; i < c->nodes.size(); ++i) {
            SDL_FRect row{list.x, list.y + static_cast<float>(i) * rowH, list.w, rowH};
            const bool selected = static_cast<int>(i) == selectedRow_;
            if (selected)
                SDL_SetRenderDrawColor(R, 60, 80, 115, 255);
            else
                SDL_SetRenderDrawColor(R, (i % 2) ? 44 : 48, (i % 2) ? 44 : 48, (i % 2) ? 49 : 53, 255);
            SDL_RenderFillRect(R, &row);
            drawText(R, row.x + 6.f, row.y + 3.f,
                     std::to_string(i + 1) + ": " + typeName(c->nodes[i]),
                     SDL_Color{210, 210, 215, 255});
        }
    }

    // Drop indicator while dragging a node up/down the list.
    if (draggingNode_ >= 0 && dragTargetInsert_ >= 0 && c) {
        const int target = std::clamp(dragTargetInsert_, 0, static_cast<int>(c->nodes.size()));
        const float iy = list.y + static_cast<float>(target) * rowH - 1.f;
        SDL_SetRenderDrawColor(R, 255, 200, 50, 255);
        SDL_RenderLine(R, list.x, iy, list.x + list.w, iy);
    }

    SDL_FRect addRow{list.x, list.y + static_cast<float>(c ? c->nodes.size() : 0) * rowH,
                     list.w, rowH};
    if (addRow.y + addRow.h < list.y + list.h) {
        SDL_SetRenderDrawColor(R, 60, 90, 140, 255);
        SDL_RenderFillRect(R, &addRow);
        drawText(R, addRow.x + 6.f, addRow.y + 3.f, "+ Add Insert", SDL_Color{235, 235, 235, 255});
    }

    Node* selected = nullptr;
    if (c && selectedRow_ >= 0 && selectedRow_ < static_cast<int>(c->nodes.size()))
        selected = c->nodes[static_cast<size_t>(selectedRow_)];
    renderPreview(R, selected);
}

void MixerChainWindow::renderPreview(SDL_Renderer* R, Node* sel) {
    // Param textures cache their creating renderer. A new chain window has a new renderer,
    // so drop the cached textures when it changes or knobs draw as garbage.
    if (previewRenderer_ != R) {
        previewRenderer_ = R;
        if (previewTex_) {
            SDL_DestroyTexture(previewTex_);
            previewTex_ = nullptr;
        }
        if (mixerNode) {
            if (auto* c = mixerNode->chainByID(chainID)) {
                for (Node* n : c->nodes) {
                    if (!n) continue;
                    n->clearParamTextures();
                    n->clearCustomTextures();
                }
            }
        }
    }

    const SDL_FRect preview{258.f, 30.f, 200.f, 180.f};
    SDL_SetRenderDrawColor(R, 26, 26, 30, 255);
    SDL_RenderFillRect(R, &preview);
    SDL_SetRenderDrawColor(R, 90, 90, 100, 255);
    SDL_RenderRect(R, &preview);

    if (!sel) {
        drawText(R, preview.x + 10.f, preview.y + 10.f, "(no insert selected)",
                 SDL_Color{150, 150, 160, 255});
        return;
    }

    // Node content draws in local node coordinates (0..NODE_W/H); the viewport maps
    // those onto the preview rectangle.
    // Render the node's local-coordinate content into an offscreen texture, then blit.
    // (Changing the window viewport per frame made the window flicker on this backend.)
    if (!previewTex_) {
        previewTex_ = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                        static_cast<int>(preview.w), static_cast<int>(preview.h));
    }
    if (previewTex_) {
        SDL_SetRenderTarget(R, previewTex_);
        SDL_SetRenderDrawColor(R, 52, 52, 56, 255);
        SDL_FRect local{0.f, 0.f, preview.w, preview.h};
        SDL_RenderFillRect(R, &local);
        sel->renderContent(R);
        SDL_SetRenderTarget(R, nullptr);
        SDL_RenderTexture(R, previewTex_, nullptr, &preview);
    }

    SDL_SetRenderDrawColor(R, 90, 90, 100, 255);
    SDL_RenderRect(R, &preview);
    drawText(R, preview.x, preview.y + preview.h + 6.f,
             typeName(sel) + " — selected insert", SDL_Color{210, 210, 215, 255});
}

bool MixerChainWindow::handleInput(SDL_Event& e) {
    if (!mixerNode) return false;

    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    int wx = 0, wy = 0;
    SDL_GetWindowPosition(window, &wx, &wy);

    float gx = 0.f, gy = 0.f;
    SDL_GetGlobalMouseState(&gx, &gy);
    const float mx = gx - static_cast<float>(wx);
    const float my = gy - static_cast<float>(wy);

    MixerNode::Chain* c = mixerNode->chainByID(chainID);
    if (!c) return false;

    const float rowH = 22.f;
    const float listTop = 30.f;
    const float listBottom = static_cast<float>(wh) - 40.f;
    const float listRight = 6.f + 240.f;

    // Selected insert preview: forward the event to that node's own GUI. Node content
    // is laid out in local node coordinates, so translate into the preview rect.
    {
        const SDL_FRect preview{258.f, 30.f, 200.f, 180.f};
        Node* sel = (!c->nodes.empty() && selectedRow_ >= 0 &&
                     selectedRow_ < static_cast<int>(c->nodes.size()))
            ? c->nodes[static_cast<size_t>(selectedRow_)] : nullptr;
        const bool overPreview = mx >= preview.x && mx <= preview.x + preview.w &&
                                 my >= preview.y && my <= preview.y + preview.h;
        if (sel && (overPreview || previewDragging_)) {
            sel->msX = mx - preview.x;
            sel->msY = my - preview.y;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
                previewDragging_ = true;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
                sel->openParameterMenuAt(e, sel->msX, sel->msY);
            } else {
                sel->customInputHandled_ = false;
                sel->handleWindowInput(e);
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                previewDragging_ = false;
            return true;
        }
    }

    // Scroll over a row moves that node up/down the insert order.
    if (e.type == SDL_EVENT_MOUSE_WHEEL && my >= listTop && my <= listBottom &&
        mx >= 6.f && mx <= listRight && e.wheel.y != 0.f) {
        const int row = static_cast<int>((my - listTop) / rowH);
        const int count = static_cast<int>(c->nodes.size());
        if (row >= 0 && row < count) {
            const int target = std::clamp(row + (e.wheel.y > 0.f ? -1 : 1), 0, count - 1);
            if (target != row) {
                Node* moved = c->nodes[static_cast<size_t>(row)];
                c->nodes.erase(c->nodes.begin() + row);
                c->nodes.insert(c->nodes.begin() + target, moved);
                c->appliedInCh = -1;
                if (mixerNode->nm) mixerNode->nm->markTopologyDirty();
                // Keep the cursor over the node that moved.
                const float newY = my + static_cast<float>(target - row) * rowH;
                SDL_WarpMouseInWindow(window, mx, newY);
            }
        }
        return true;
    }

    // Drag a row to reorder the insert list.
    if (draggingNode_ >= 0) {
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            int target = static_cast<int>((my - listTop) / rowH + 0.5f);
            dragTargetInsert_ = std::clamp(target, 0, static_cast<int>(c->nodes.size()));
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            const int from = draggingNode_;
            const int to = dragTargetInsert_;
            draggingNode_ = -1;
            dragTargetInsert_ = -1;
            if (from >= 0 && from < static_cast<int>(c->nodes.size()) &&
                to >= 0 && to <= static_cast<int>(c->nodes.size()) && from != to) {
                std::vector<Node*> reordered = c->nodes;
                Node* moved = reordered[static_cast<size_t>(from)];
                reordered.erase(reordered.begin() + from);
                const int insertAt = (to > from) ? (to - 1) : to;
                reordered.insert(reordered.begin() + insertAt, moved);
                c->nodes = std::move(reordered);
                c->appliedInCh = -1; // input wiring order changed
                if (mixerNode->nm) mixerNode->nm->markTopologyDirty();
            }
            return true;
        }
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && my >= listTop && my <= listBottom &&
        mx >= 6.f && mx <= listRight) {
        const int row = static_cast<int>((my - listTop) / rowH);
        const int count = static_cast<int>(c->nodes.size());

        if (e.button.button == SDL_BUTTON_RIGHT && row >= 0 && row < count) {
            auto* ctx = ContextMenu::get();
            ctx->activate();
            auto root = uTreeEntry();
            auto del = uTreeEntry();
            del->label = "Remove " + typeName(c->nodes[static_cast<size_t>(row)]);
            const int nodeIndex = row;
            del->click = [this, nodeIndex]() {
                for (size_t ci = 0; ci < mixerNode->chains.size(); ++ci)
                    if (mixerNode->chains[ci]->id == chainID) {
                        mixerNode->removeNodeFromChain(static_cast<int>(ci), nodeIndex);
                        break;
                    }
            };
            root->addChild(del);
            ctx->dynamicTick = getTreeMenuTicker(root);
            return true;
        }

        if (e.button.button == SDL_BUTTON_LEFT && row >= 0 && row < count) {
            selectedRow_ = row;
            draggingNode_ = row;
            dragTargetInsert_ = row;
            return true;
        }

        if (e.button.button == SDL_BUTTON_LEFT && row == count) {
            auto* ctx = ContextMenu::get();
            ctx->activate();
            auto root = uTreeEntry();
            for (int t = 0; t < static_cast<int>(NodeType::Count); ++t) {
                if (t == static_cast<int>(NodeType::Arranger) ||
                    t == static_cast<int>(NodeType::Patcher) ||
                    t == static_cast<int>(NodeType::Multiplexer) ||
                    t == static_cast<int>(NodeType::Mixer))
                    continue;
                auto entry = uTreeEntry();
                entry->label = std::string("Add ") + NodeTypeStr[t];
                entry->click = [this, t]() {
                    for (size_t ci = 0; ci < mixerNode->chains.size(); ++ci)
                        if (mixerNode->chains[ci]->id == chainID) {
                            mixerNode->addNodeToChain(static_cast<int>(ci), static_cast<NodeType>(t));
                            break;
                        }
                };
                root->addChild(entry);
            }
            ctx->dynamicTick = getTreeMenuTicker(root);
            return true;
        }
        return true;
    }

    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
        onRequestClose();
        return true;
    }
    return false;
}

// ============================================================================
// Serialization
// ============================================================================

json MixerNode::extraSerialize() {
    json j;
    j["chainIdPool"] = chainIdPool_.toJSON();
    j["nodeIdPool"] = nodeIdPool_.toJSON();
    j["chains"] = json::array();
    for (auto& cp : chains) {
        Chain& c = *cp;
        json jc;
        jc["id"] = c.id;
        if (c.inflow) jc["inputConnectionID"] = c.inflow->id;
        jc["pan"] = c.pan.value;
        jc["vol"] = c.volume.value;
        jc["solo"] = c.solo;
        jc["mute"] = c.mute;
        jc["nodes"] = json::array();
        for (Node* n : c.nodes)
            if (n) jc["nodes"].push_back(n->serialize());
        j["chains"].push_back(std::move(jc));
    }
    return j;
}

void MixerNode::extraDeSerialize(const json& j) {
    for (auto& cp : chains) {
        for (Node* n : cp->nodes) destroyNode(n);
        removeInputPort(cp->inflow);
    }
    chains.clear();

    if (j.contains("chainIdPool")) chainIdPool_.fromJSON(j["chainIdPool"]);
    if (j.contains("nodeIdPool")) nodeIdPool_.fromJSON(j["nodeIdPool"]);

    if (j.contains("chains") && j["chains"].is_array()) {
        for (const auto& jc : j["chains"]) {
            auto c = std::make_unique<Chain>();
            c->id = jc.value("id", -1);
            if (c->id < 0) c->id = static_cast<int>(chainIdPool_.newID());
            else chainIdPool_.reserveID(static_cast<uint16_t>(c->id));
            c->inflow = addInputPort(static_cast<uint16_t>(jc.value("inputConnectionID", 0)));
            c->pan.value = jc.value("pan", 0.5f);
            c->volume.value = jc.value("vol", 1.0f);
            c->solo = jc.value("solo", false);
            c->mute = jc.value("mute", false);
            if (jc.contains("nodes") && jc["nodes"].is_array()) {
                for (const auto& jn : jc["nodes"]) {
                    json jn2;
                    resolveChainNodeID(nm, jn, jn2);
                    g_vstLoadingChainInsert = true;
                    Node* n = Node::deSerialize(jn2, nm, false);
                    g_vstLoadingChainInsert = false;
                    if (!n) continue;
                    nodeIdPool_.reserveID(n->id);
                    if (nm) nm->registerExtraNode(n);
                    stampChainOwnership(n, id, c->id);
                    c->nodes.push_back(n);
                }
            }
            chains.push_back(std::move(c));
        }
    }

    if (chains.empty()) {
        // Deserialization: direct creation on this graph (no undo, no mirroring).
        addChainOnGraph(static_cast<int>(chainIdPool_.newID()), 0);
        addChainOnGraph(static_cast<int>(chainIdPool_.newID()), 0);
    }
    syncChainParams();
    if (nm) nm->markTopologyDirty();
}
