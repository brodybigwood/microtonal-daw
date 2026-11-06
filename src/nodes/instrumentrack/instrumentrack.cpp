#include "instrumentrack.h"
#include "NodeManager.h"
#include "NodeProcessor.h"
#include "UndoManager.h"
#include "ContextMenu.h"
#include "TreeEntry.h"
#include "styles.h"
#include "nodes/nodetypes.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numeric>

// ============================================================================
// Helpers
// ============================================================================

static Connection* firstEventIn(Node* n) {
    for (auto* c : n->inputs.connections)
        if (c->type == DataType::Events) return c;
    return nullptr;
}

static Connection* firstWaveOut(Node* n) {
    for (auto* c : n->outputs.connections)
        if (c->type == DataType::Waveform) return c;
    return nullptr;
}

static float columnLeft(size_t i, float scroll) {
    return InstrumentRackNode::kColPad + static_cast<float>(i) * (InstrumentRackNode::kColW + InstrumentRackNode::kColPad) - scroll;
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

// Insert list label: a VST shows the plugin (class) it has loaded, not "VST".
static std::string insertLabel(const Node* n) {
    if (n->nodeType == NodeType::MpeRack && !n->name.empty()) return n->name + " (MPE)";
    if (n->nodeType == NodeType::Vst && !n->name.empty()) return n->name;
    return typeName(n);
}

// ============================================================================
// InstrumentRackNode::Track
// ============================================================================

InstrumentRackNode::Track::Track()
    : pan(0.5f, 0.f, 0.f, 8.f, "assets/knobs/1.png", -135.f, 135.f, "Pan"),
      volume(1.0f, 0.f, 0.f, 8.f, 40.f) {
    pan.displayLabel = "Pan";
}

// ============================================================================
// Construction
// ============================================================================

InstrumentRackNode::InstrumentRackNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::InstrumentRack) {
    name = "Instrument Rack";

    vCount = 4;
    vx = new float[4]{0.f, NODE_W, NODE_W, 0.f};
    vy = new float[4]{0.f, 0.f, kNODE_H, kNODE_H};
    dstRect.h = kNODE_H;
    w = NODE_W;
    h = kNODE_H;

    // One multichannel input per track; each is routed into that track's bottom node.
    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    out->minChannels = 1;
    out->updateNumChannels();
    outputs.addConnection(out);

    // Fresh node: create tracks directly; there is nothing to mirror or undo yet.
    addTrackOnGraph(static_cast<int>(trackIdPool_.newID()), 0);
    addTrackOnGraph(static_cast<int>(trackIdPool_.newID()), 0);
    addTrackOnGraph(static_cast<int>(trackIdPool_.newID()), 0);
    addTrackOnGraph(static_cast<int>(trackIdPool_.newID()), 0);
}

InstrumentRackNode::~InstrumentRackNode() {
    closeTrackWindow();
    for (auto& cp : tracks)
        for (Node* n : cp->nodes) destroyNode(n);
    tracks.clear();
    if (vx) delete[] vx;
    if (vy) delete[] vy;
    vx = nullptr;
    vy = nullptr;
}

Connection* InstrumentRackNode::addInputPort(uint16_t forcedID) {
    auto* c = new Connection;
    c->type = DataType::Events;
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

void InstrumentRackNode::removeInputPort(Connection* c) {
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

Connection* InstrumentRackNode::addOutputPort(uint16_t forcedID) {
    auto* c = new Connection;
    c->type = DataType::Waveform;
    c->dir = Direction::output;
    c->minChannels = 1;
    c->updateNumChannels();
    if (forcedID == 0) {
        outputs.addConnection(c); // allocates the id
        return c;
    }
    c->nm = outputs.nm;
    c->id = forcedID;
    c->output_connection = c->id;
    c->output_node = outputs.nodeID;
    c->input_connection = -1;
    c->input_node = -1;
    c->events = nullptr;
    c->buffer = nullptr;
    c->bufferSize = 0;
    c->allocChannels = c->numChannels;
    outputs.connections.push_back(c);
    outputs.id_pool.reserveID(c->id);
    outputs.ids[c->id] = static_cast<uint16_t>(outputs.connections.size() - 1);
    return c;
}

void InstrumentRackNode::removeOutputPort(Connection* c) {
    if (!c) return;
    for (size_t i = 0; i < outputs.connections.size(); ++i) {
        if (outputs.connections[i] != c) continue;
        outputs.connections.erase(outputs.connections.begin() + static_cast<ptrdiff_t>(i));
        break;
    }
    outputs.id_pool.releaseID(c->id);
    outputs.ids.erase(c->id);
    if (c->buffer) delete[] c->buffer;
    delete c;
}

void InstrumentRackNode::addTrack(uint16_t inputPortID) {
    // Keep track numbers compact: reuse the lowest free id so a track added
    // after a deletion takes the next number in order instead of the pool's
    // ever-increasing counter.
    int cid = 0;
    while (std::any_of(tracks.begin(), tracks.end(),
                       [cid](const std::unique_ptr<Track>& t) { return t && t->id == cid; }))
        ++cid;
    if (!project || !project->um || !nm) {
        addTrackOnGraph(cid, inputPortID, 0);
        return;
    }
    const uint16_t pid = inputPortID != 0 ? inputPortID : inputs.id_pool.newID();
    const uint16_t oid = outputs.id_pool.newID();
    project->um->newAction(new InstrumentRackEditUndoAction(project, nm->managerPath,
        static_cast<int>(id), InstrumentRackEditUndoAction::Add, cid, pid, oid, {}, json(),
        "Add Rack Track"));
}

void InstrumentRackNode::removeTrack(int index) {
    if (index < 0 || index >= static_cast<int>(tracks.size())) return;
    const int cid = tracks[static_cast<size_t>(index)]->id;
    if (!project || !project->um || !nm) {
        removeTrackOnGraph(cid);
        return;
    }
    project->um->newAction(new InstrumentRackEditUndoAction(project, nm->managerPath,
        static_cast<int>(id), InstrumentRackEditUndoAction::Remove, cid, 0, 0, {}, trackJson(cid),
        "Remove Rack Track"));
}

InstrumentRackNode::Track* InstrumentRackNode::trackByID(int trackID) {
    for (auto& c : tracks)
        if (c->id == trackID) return c.get();
    return nullptr;
}

Node* InstrumentRackNode::createNode(NodeType type) {
    const uint16_t nid = nm ? nm->allocateNodeID()
                            : static_cast<uint16_t>(nodeIdPool_.newID());
    Node* n = byType(type, static_cast<int>(nid), nm);
    if (!n) return nullptr;
    if (nm) nm->registerExtraNode(n);
    n->update(bufferSize, sampleRate);
    n->setup();
    return n;
}

// Track inserts share the owning manager's id namespace, so actions can resolve them
// through the normal managerPath + nodeID lookup. Saved tracks may carry ids from the
// old per-track pool; renumber any that collide with a graph node or system node.
static uint16_t resolveTrackNodeID(NodeManager* nm, const json& jn, json& out) {
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

static void stampTrackOwnership(Node* n, uint16_t rackID, int trackID) {
    if (!n) return;
    n->mixerOwnerNodeID = static_cast<int>(rackID);
    n->mixerOwnerChainID = trackID;
    // Nested graphs (Delay feedback patcher) need an address that resolves through the
    // rack track: [rackID, trackID, insertID].
    if (auto* d = dynamic_cast<DelayNode*>(n); d && d->chain) {
        std::vector<int> path = n->nm ? n->nm->managerPath : std::vector<int>{};
        path.push_back(static_cast<int>(rackID));
        path.push_back(trackID);
        path.push_back(static_cast<int>(n->id));
        d->chain->managerPath = std::move(path);
    }
}

Node* InstrumentRackNode::trackNodeByID(int trackID, int nodeID) {
    Track* c = trackByID(trackID);
    if (!c) return nullptr;
    for (Node* n : c->nodes)
        if (n && static_cast<int>(n->id) == nodeID) return n;
    return nullptr;
}

Node* InstrumentRackNode::insertTrackNodeFromSnapshot(int trackID, const json& nodeSnapshot) {
    Track* c = trackByID(trackID);
    if (!c || !nm) return nullptr;
    g_vstLoadingChainInsert = true;
    Node* n = Node::deSerialize(nodeSnapshot, nm, false);
    g_vstLoadingChainInsert = false;
    if (!n) return nullptr;
    nm->reserveNodeID(n->id);
    nm->registerExtraNode(n);
    stampTrackOwnership(n, id, trackID);
    c->nodes.push_back(n);
    c->appliedInCh = -1;
    nm->markTopologyDirty();
    return n;
}

bool InstrumentRackNode::removeTrackNodeByID(int trackID, int nodeID) {
    Track* c = trackByID(trackID);
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

void InstrumentRackNode::destroyNode(Node* n) {
    if (!n) return;
    // Expanded windows owned by the insert (e.g. the Delay feedback patcher)
    // hold pointers to it; close them before deleting.
    if (project && project->processor) {
        if (auto* wm = project->processor->getWindowManager()) {
            std::vector<ExpandedWindow*> owned;
            for (auto& w : wm->windows())
                if (w && w->ownerNode == n) owned.push_back(w.get());
            for (auto* w : owned) wm->removeWindow(w);
        }
    }
    if (nm) nm->unregisterExtraNode(n);
    delete n;
}

json InstrumentRackNode::trackNodesJson(int trackID) const {
    json arr = json::array();
    for (const auto& cp : tracks) {
        if (cp->id != trackID) continue;
        for (Node* n : cp->nodes)
            if (n) arr.push_back(n->serialize());
        break;
    }
    return arr;
}

void InstrumentRackNode::applyTrackNodes(int trackID, const json& nodesJson) {
    Track* c = trackByID(trackID);
    if (!c) return;
    for (Node* n : c->nodes) destroyNode(n);
    c->nodes.clear();
    if (nodesJson.is_array()) {
        for (const auto& jn : nodesJson) {
            json jn2;
            resolveTrackNodeID(nm, jn, jn2);
            g_vstLoadingChainInsert = true;
            Node* n = Node::deSerialize(jn2, nm, false);
            g_vstLoadingChainInsert = false;
            if (!n) continue;
            nodeIdPool_.reserveID(n->id);
            if (nm) nm->registerExtraNode(n);
            stampTrackOwnership(n, id, trackID);
            c->nodes.push_back(n);
        }
    }
    // Processing order is the vector order; no explicit wiring to rebuild.
    c->appliedInCh = -1;
    if (nm) nm->markTopologyDirty();
}

int InstrumentRackNode::addNodeToTrack(int trackIndex, NodeType type) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size())) return -1;
    Track* c = tracks[trackIndex].get();
    Node* temp = createNode(type);
    if (!temp) return -1;
    const int addedID = static_cast<int>(temp->id);
    json nodeSnapshot = temp->serialize();
    destroyNode(temp);  // the action re-creates it from the snapshot

    if (!project || !project->um || !nm) {
        return insertTrackNodeFromSnapshot(c->id, nodeSnapshot) ? addedID : -1;
    }
    project->um->newAction(new InstrumentRackInsertNodeAction(project, nm->managerPath,
        static_cast<int>(id), c->id, std::move(nodeSnapshot)));
    return addedID;
}

void InstrumentRackNode::removeNodeFromTrack(int trackIndex, int nodeIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size())) return;
    Track* c = tracks[trackIndex].get();
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(c->nodes.size())) return;
    Node* n = c->nodes[static_cast<size_t>(nodeIndex)];
    if (!n) return;
    json nodeSnapshot = n->serialize();
    const int nid = static_cast<int>(n->id);
    if (project && project->um && nm) {
        project->um->newAction(new InstrumentRackInsertNodeAction(project, nm->managerPath,
            static_cast<int>(id), c->id, std::move(nodeSnapshot), /*removeOnDo=*/true,
            "Rack Remove Instrument"));
    } else {
        removeTrackNodeByID(c->id, nid);
    }
}

void InstrumentRackNode::syncTrackParams() {
    // Per-track pan/volume live in the node's parameter list (pan at 2*i,
    // volume at 2*i+1) so SetParamValueUndoAction can drive them.
    params.clear();
    params.reserve(tracks.size() * 2);
    for (size_t i = 0; i < tracks.size(); ++i) {
        auto& cp = tracks[i];
        cp->volume.bind(this, i * 2 + 1);
        params.push_back(&cp->pan);
        params.push_back(&cp->volume);
    }
}

void InstrumentRackNode::addTrackOnGraph(int trackID, uint16_t portID, uint16_t outPortID) {
    if (trackByID(trackID)) return;
    auto c = std::make_unique<Track>();
    c->id = trackID;
    trackIdPool_.reserveID(static_cast<uint16_t>(trackID));
    c->inflow = addInputPort(portID);
    c->outflow = addOutputPort(outPortID);
    tracks.push_back(std::move(c));
    syncTrackParams();
    makeConnectionRects();
    if (nm) nm->markTopologyDirty();
}

void InstrumentRackNode::removeTrackOnGraph(int trackID) {
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i]->id != trackID) continue;
        for (Node* n : tracks[i]->nodes) destroyNode(n);
        removeInputPort(tracks[i]->inflow);
        removeOutputPort(tracks[i]->outflow);
        tracks.erase(tracks.begin() + static_cast<ptrdiff_t>(i));
        syncTrackParams();
        makeConnectionRects();
        if (nm) nm->markTopologyDirty();
        return;
    }
}

void InstrumentRackNode::reorderTracksOnGraph(const std::vector<int>& trackIDs) {
    if (trackIDs.size() != tracks.size()) return;

    // Resolve every id to an index first, so a missing id cannot leave the
    // vector partially moved/erased.
    std::vector<size_t> order;
    order.reserve(trackIDs.size());
    std::vector<bool> used(tracks.size(), false);
    for (int cid : trackIDs) {
        size_t found = SIZE_MAX;
        for (size_t i = 0; i < tracks.size(); ++i) {
            if (!used[i] && tracks[i] && tracks[i]->id == cid) { found = i; break; }
        }
        if (found == SIZE_MAX) return;
        used[found] = true;
        order.push_back(found);
    }

    std::vector<std::unique_ptr<Track>> reordered;
    reordered.reserve(tracks.size());
    for (size_t idx : order) reordered.push_back(std::move(tracks[idx]));
    tracks = std::move(reordered); // pointers only

    // Keep the input-port row in the same order as the track columns. Port
    // objects and ids are unchanged, so existing cables stay valid.
    std::vector<Connection*> newInputs;
    newInputs.reserve(inputs.connections.size());
    for (auto& cp : tracks)
        if (cp->inflow) newInputs.push_back(cp->inflow);
    for (Connection* c : inputs.connections) {
        if (std::find(newInputs.begin(), newInputs.end(), c) != newInputs.end()) continue;
        newInputs.push_back(c); // e.g. mapped-parameter ports
    }
    inputs.connections = std::move(newInputs);
    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);

    // Per-track output ports follow the same order as the tracks.
    std::vector<Connection*> newOutputs;
    newOutputs.reserve(outputs.connections.size());
    for (auto& cp : tracks)
        if (cp->outflow) newOutputs.push_back(cp->outflow);
    for (Connection* c : outputs.connections) {
        if (std::find(newOutputs.begin(), newOutputs.end(), c) != newOutputs.end()) continue;
        newOutputs.push_back(c); // master output
    }
    outputs.connections = std::move(newOutputs);
    outputs.ids.clear();
    for (size_t i = 0; i < outputs.connections.size(); ++i)
        outputs.ids[outputs.connections[i]->id] = static_cast<uint16_t>(i);

    syncTrackParams();
    makeConnectionRects();
    if (nm) nm->markTopologyDirty();
}

json InstrumentRackNode::trackJson(int trackID) const {
    json jc;
    for (const auto& cp : tracks) {
        if (cp->id != trackID) continue;
        const Track& c = *cp;
        jc["id"] = c.id;
        if (c.inflow) jc["inputConnectionID"] = c.inflow->id;
        if (c.outflow) jc["outputConnectionID"] = c.outflow->id;
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

void InstrumentRackNode::restoreTrack(const json& jc) {
    const int cid = jc.value("id", -1);
    if (cid < 0 || trackByID(cid)) return;
    auto c = std::make_unique<Track>();
    c->id = cid;
    trackIdPool_.reserveID(static_cast<uint16_t>(cid));
    c->inflow = addInputPort(static_cast<uint16_t>(jc.value("inputConnectionID", 0)));
    c->outflow = addOutputPort(static_cast<uint16_t>(jc.value("outputConnectionID", 0)));
    c->pan.value = jc.value("pan", 0.5f);
    c->volume.value = jc.value("vol", 1.0f);
    c->solo = jc.value("solo", false);
    c->mute = jc.value("mute", false);
    if (jc.contains("nodes") && jc["nodes"].is_array()) {
        for (const auto& jn : jc["nodes"]) {
            json jn2;
            resolveTrackNodeID(nm, jn, jn2);
            g_vstLoadingChainInsert = true;
            Node* n = Node::deSerialize(jn2, nm, false);
            g_vstLoadingChainInsert = false;
            if (!n) continue;
            nodeIdPool_.reserveID(n->id);
            if (nm) nm->registerExtraNode(n);
            stampTrackOwnership(n, id, cid);
            c->nodes.push_back(n);
        }
    }
    tracks.push_back(std::move(c));
    syncTrackParams();
    if (nm) nm->markTopologyDirty();
}

void InstrumentRackNode::reorderTracks(const std::vector<size_t>& newOrder) {
    if (newOrder.size() != tracks.size()) return;
    if (!project || !project->um || !nm) {
        std::vector<int> ids;
        for (size_t idx : newOrder) ids.push_back(tracks[idx]->id);
        reorderTracksOnGraph(ids);
        return;
    }
    std::vector<int> ids;
    for (size_t idx : newOrder) ids.push_back(tracks[idx]->id);
    project->um->newAction(new InstrumentRackEditUndoAction(project, nm->managerPath,
        static_cast<int>(id), InstrumentRackEditUndoAction::Reorder, 0, 0, 0, std::move(ids), json(),
        "Reorder Rack Tracks"));
}

// ============================================================================
// Audio
// ============================================================================

void InstrumentRackNode::setup() {
    if (!out) return;
    // Master output.
    out->minChannels = std::max(1, outChannelsTarget_);
    out->updateNumChannels();
    if (bufferSize > 0 && (out->buffer == nullptr || out->allocChannels != out->numChannels))
        out->allocateBuffer(bufferSize, out->numChannels);

    // Per-track outputs are sized from their instruments' current output widths.
    // Allocation happens here (not in process): reallocating a buffer that a
    // downstream node already holds a pointer to corrupts the audio pass.
    for (auto& cp : tracks) {
        Track& c = *cp;
        int ch = 1;
        for (Node* n : c.nodes) {
            Connection* dout = n ? firstWaveOut(n) : nullptr;
            if (dout) ch = std::max(ch, std::max(1, static_cast<int>(dout->numChannels)));
        }
        c.channels = ch;
        if (!c.outflow) continue;
        c.outflow->minChannels = ch;
        c.outflow->numChannels = ch;
        c.outflow->updateNumChannels();
        if (bufferSize > 0 &&
            (c.outflow->buffer == nullptr || c.outflow->bufferSize != bufferSize ||
             c.outflow->allocChannels != c.outflow->numChannels))
            c.outflow->allocateBuffer(bufferSize, c.outflow->numChannels);
    }
}

void InstrumentRackNode::process() {
    if (!out || !out->buffer || bufferSize <= 0) return;

    const int bs = bufferSize;
    const int sr = sampleRate > 0 ? sampleRate : 48000;

    if (std::getenv("DAW_VST_DEBUG")) {
        static uint64_t lastDbg = 0;
        const uint64_t nowDbg = SDL_GetTicks();
        if (nowDbg - lastDbg >= 1000) {
            lastDbg = nowDbg;
            std::cout << "[rack] node=" << id << " tracks=" << tracks.size();
            for (auto& cp : tracks) {
                const size_t evs = (cp->inflow && cp->inflow->events)
                    ? cp->inflow->events->size() : static_cast<size_t>(-1);
                std::cout << " t" << cp->id << " nodes=" << cp->nodes.size()
                          << " evs=" << evs
                          << " out=" << (cp->outflow && cp->outflow->buffer ? "buf" : "null");
                for (Node* n : cp->nodes) {
                    if (!n) continue;
                    Connection* dout = firstWaveOut(n);
                    std::cout << " [" << n->name << " out="
                              << (dout && dout->buffer ? "buf" : "null") << "]";
                }
            }
            std::cout << std::endl;
        }
    }

    int maxCh = 1;
    int maxLat = 0;

    // Each track reads its own event input. Every instrument in the track is fed
    // that same event stream in parallel; no audio runs between the instruments.
    for (auto& cp : tracks) {
        Track& c = *cp;
        std::vector<Event>* evs =
            (c.inflow && c.inflow->is_connected && c.inflow->events) ? c.inflow->events : nullptr;

        for (Node* n : c.nodes) {
            if (!n) continue;
            Connection* ein = firstEventIn(n);
            for (Connection* ci : n->inputs.connections) {
                if (ci->type == DataType::Events) {
                    const bool use = (ci == ein) && evs != nullptr;
                    ci->is_connected = use;
                    ci->events = use ? evs : nullptr;
                } else {
                    // Parallel instruments take no audio input from the track.
                    ci->is_connected = false;
                    ci->buffer = nullptr;
                    ci->sourceChannels = 0;
                }
            }
            Connection* dout = firstWaveOut(n);
            if (!dout) continue;
            if (dout->buffer == nullptr || dout->bufferSize != bs)
                n->update(bs, sr);
            n->process();
            dout->latency = std::max(0, n->reportedLatency());
            maxLat = std::max(maxLat, static_cast<int>(dout->latency));
            if (dout->buffer)
                maxCh = std::max(maxCh, std::max(1, static_cast<int>(dout->numChannels)));
        }
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
    for (auto& cp : tracks) {
        if (cp->solo && !cp->mute) { anySolo = true; break; }
    }

    for (auto& cp : tracks) {
        Track& c = *cp;

        // Sum this track's instruments into c.buf, delaying each by its share of
        // the rack latency so every parallel path stays sample-aligned.
        int ch = 1;
        for (Node* n : c.nodes) {
            Connection* dout = n ? firstWaveOut(n) : nullptr;
            if (dout && dout->buffer)
                ch = std::max(ch, std::max(1, static_cast<int>(dout->numChannels)));
        }
        c.channels = ch;
        c.latency = maxLat;
        c.buf.assign(static_cast<size_t>(ch) * static_cast<size_t>(bs), 0.0f);

        for (Node* n : c.nodes) {
            Connection* dout = n ? firstWaveOut(n) : nullptr;
            if (!dout || !dout->buffer) continue;
            const int nch = std::max(1, static_cast<int>(dout->numChannels));
            const int d = std::max(0, maxLat - static_cast<int>(dout->latency));

            // Align this instrument's output through its own connection PDC line.
            if (d != dout->pdcDelay) dout->setPdcDelay(d);
            if (d > 0) {
                const int chans = nch;
                if (static_cast<int>(dout->pdcLine.size()) != chans ||
                    static_cast<int>(dout->pdcLine[0].size()) != d) {
                    dout->pdcLine.assign(static_cast<size_t>(chans),
                                         std::vector<float>(static_cast<size_t>(d), 0.0f));
                    dout->pdcWrite = 0;
                }
                if (dout->pdcBuffer.size() < static_cast<size_t>(chans) * static_cast<size_t>(bs))
                    dout->pdcBuffer.assign(static_cast<size_t>(chans) * static_cast<size_t>(bs), 0.0f);
                for (int cc = 0; cc < chans; ++cc) {
                    const float* src = dout->buffer + static_cast<size_t>(cc) * static_cast<size_t>(bs);
                    float* dst = dout->pdcBuffer.data() + static_cast<size_t>(cc) * static_cast<size_t>(bs);
                    std::vector<float>& ring = dout->pdcLine[static_cast<size_t>(cc)];
                    for (int i = 0; i < bs; ++i) {
                        const int idx = (dout->pdcWrite + i) % d;
                        dst[i] = ring[static_cast<size_t>(idx)];
                        ring[static_cast<size_t>(idx)] = src[i];
                    }
                }
                dout->pdcWrite = (dout->pdcWrite + bs) % d;
            }

            const int useCh = std::min(nch, ch);
            for (int cc = 0; cc < useCh; ++cc) {
                const float* src = dout->channel(cc);
                float* dst = c.buf.data() + static_cast<size_t>(cc) * static_cast<size_t>(bs);
                for (int i = 0; i < bs; ++i) dst[i] += src[i];
            }
        }

        // This track's own audio output carries its instrument sum. Mute/solo only
        // affect the contribution to the master output below.
        if (c.outflow && c.outflow->buffer && c.outflow->bufferSize == bs &&
            c.outflow->allocChannels >= ch) {
            std::memcpy(c.outflow->buffer, c.buf.data(),
                        static_cast<size_t>(ch) * static_cast<size_t>(bs) * sizeof(float));
        }

        if (c.nodes.empty() || c.mute) continue;
        if (anySolo && !c.solo) continue;

        const float panL = std::cos(c.pan.value * static_cast<float>(M_PI_2));
        const float panR = std::sin(c.pan.value * static_cast<float>(M_PI_2));
        const float vol = c.volume.value;
        const int nch = std::min(c.channels, outCh);
        for (int cc = 0; cc < nch; ++cc) {
            const float pg = (nch >= 2) ? ((cc == 0) ? panL : panR) : 1.f;
            const float gain = vol * pg;
            const float* src = c.buf.data() + static_cast<size_t>(cc) * static_cast<size_t>(bs);
            float* dst = out->channel(cc);
            for (int i = 0; i < bs; ++i) dst[i] += src[i] * gain;
        }
    }
}

// ============================================================================
// Content rendering (same column layout as the sampler, pan knob in place of
// the waveform thumbnail; each column is one track).
// ============================================================================

void InstrumentRackNode::render(SDL_Renderer* R) {
    if (!R) return;
    if (!texture || !dstRect.h) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_TARGET, static_cast<int>(NODE_W), static_cast<int>(kNODE_H));
    }
    Node::render(R);
}

void InstrumentRackNode::renderContent(SDL_Renderer* R) {
    SDL_SetRenderDrawColor(R, 35, 35, 40, 255);
    SDL_FRect bg{0, 0, NODE_W, kNODE_H};
    SDL_RenderFillRect(R, &bg);

    const float listTop = kListTop + 24.f;
    const float colContentH = kPanH + kRowH + 2.f * kBtnSz + 2.f + kSliderH + 4.f;

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto& c = *tracks[i];
        const float cx = columnLeft(i, scrollOffset_);
        if (cx + kColW < 0.f || cx > NODE_W) continue;

        SDL_FRect colBg{cx, listTop, kColW, colContentH};
        SDL_SetRenderDrawColor(R, 50, 50, 55, 255);
        SDL_RenderFillRect(R, &colBg);
        if (hoveredTrackIndex_ == static_cast<int>(i))
            SDL_SetRenderDrawColor(R, 140, 180, 220, 255);
        else
            SDL_SetRenderDrawColor(R, 80, 80, 85, 255);
        SDL_RenderRect(R, &colBg);

        float cy = listTop + 2.f;

        // Pan knob replaces the old sample view.
        c.pan.reposition(cx + kColW * 0.5f, cy + kPanH * 0.5f, 8.f);
        c.pan.render(R);
        cy += kPanH;

        // Track label.
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

    // Drop indicator: where the dragged track column will land in the order.
    if (draggingTrack_ >= 0 && dragTargetInsert_ >= 0 &&
        dragTargetInsert_ <= static_cast<int>(tracks.size())) {
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

bool InstrumentRackNode::handleCustomInput(SDL_Event& e) {
    const float listTop = kListTop + 24.f;
    const float colContentH = kPanH + kRowH + 2.f * kBtnSz + 2.f + kSliderH + 4.f;

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        const float mx = msX;
        const float my = msY;

        SDL_FRect addBtn{NODE_W / 2.f - 40.f, kNODE_H - kFooterH - 4.f, 80.f, kFooterH};
        if (e.button.button == SDL_BUTTON_LEFT && inRect(mx, my, addBtn)) {
            addTrack();
            return true;
        }

        for (size_t i = 0; i < tracks.size(); ++i) {
            auto& c = *tracks[i];
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
                        static_cast<int>(id), std::move(pp), oldVal, c.pan.value, "Rack Pan"));
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
                    project->um->newAction(new InstrumentRackEditUndoAction(project, nm->managerPath,
                        static_cast<int>(id), InstrumentRackEditUndoAction::Solo, c.id, 0, 0, {}, json(),
                        "Rack Solo"));
                else
                    c.solo = !c.solo;
                return true;
            }
            if (e.button.button == SDL_BUTTON_LEFT && inRect(mx, my, muteR)) {
                if (project && project->um && nm)
                    project->um->newAction(new InstrumentRackEditUndoAction(project, nm->managerPath,
                        static_cast<int>(id), InstrumentRackEditUndoAction::Mute, c.id, 0, 0, {}, json(),
                        "Rack Mute"));
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
                del->label = "Remove Track";
                const int ci = static_cast<int>(i);
                del->click = [this, ci]() { removeTrack(ci); };
                root->addChild(del);
                ctx->dynamicTick = getTreeMenuTicker(root);
                return true;
            }

            if (e.button.button == SDL_BUTTON_LEFT) {
                draggingTrack_ = static_cast<int>(i);
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

void InstrumentRackNode::handleWindowInput(SDL_Event& e) {
    const float mx = msX;
    const float my = msY;
    const float listTop = kListTop + 24.f;

    if (draggingTrack_ >= 0) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            const int from = draggingTrack_;
            int to = dragTargetInsert_;
            draggingTrack_ = -1;
            dragTargetInsert_ = -1;
            if (to < 0) to = 0;
            if (to >= static_cast<int>(tracks.size())) to = static_cast<int>(tracks.size()) - 1;
            // Only a press-and-release without movement counts as a click. Otherwise a
            // drag that stayed inside its own column would open the track window.
            const float dx = mx - dragStartX_;
            const float dy = my - dragStartY_;
            const bool moved = dx * dx + dy * dy > 9.f;
            if (from != to && from >= 0 && to >= 0 && static_cast<size_t>(to) < tracks.size()) {
                std::vector<size_t> order(tracks.size());
                std::iota(order.begin(), order.end(), static_cast<size_t>(0));
                order.erase(order.begin() + from);
                order.insert(order.begin() + to, static_cast<size_t>(from));
                reorderTracks(order);
            } else if (!moved && from == to && from >= 0 && from < static_cast<int>(tracks.size())) {
                openTrackWindow(tracks[static_cast<size_t>(from)]->id);
            }
            return;
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            const float relX = mx + scrollOffset_ - kColPad;
            int target = static_cast<int>(relX / (kColW + kColPad));
            target = std::clamp(target, 0, static_cast<int>(tracks.size()));
            dragTargetInsert_ = target;
            return;
        }
        return;
    }

    if (draggingVolumeIdx_ >= 0 && draggingVolumeIdx_ < static_cast<int>(tracks.size())) {
        auto& c = *tracks[static_cast<size_t>(draggingVolumeIdx_)];
        c.volume.handleInputAt(e, mx, my);
        if (!c.volume.dragging()) draggingVolumeIdx_ = -1;
        return;
    }

    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        hoveredTrackIndex_ = -1;
        for (size_t i = 0; i < tracks.size(); ++i) {
            const float cx = columnLeft(i, scrollOffset_);
            if (mx >= cx && mx <= cx + kColW && my >= listTop &&
                my <= listTop + kPanH + kRowH + 2.f * kBtnSz + 2.f + kSliderH + 4.f) {
                hoveredTrackIndex_ = static_cast<int>(i);
                break;
            }
        }
    }

    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        // Wheel over a pan knob adjusts it; elsewhere it scrolls the strip.
        for (size_t i = 0; i < tracks.size(); ++i) {
            const float cx = columnLeft(i, scrollOffset_);
            SDL_FRect panR{cx + 2.f, listTop + 2.f, kColW - 4.f, kPanH - 4.f};
            if (inRect(mx, my, panR)) {
                Track& c = *tracks[i];
                const float oldVal = c.pan.value;
                c.pan.handleInput(e);
                if (std::fabs(c.pan.value - oldVal) > 0.0005f && project && project->um && nm) {
                    std::vector<size_t> pp{static_cast<size_t>(i) * 2};
                    project->um->newAction(new SetParamValueUndoAction(project, nm->managerPath,
                        static_cast<int>(id), std::move(pp), oldVal, c.pan.value, "Rack Pan"));
                }
                return;
            }
        }
        scrollOffset_ -= e.wheel.y * 30.f;
        const float totalW = static_cast<float>(tracks.size()) * (kColW + kColPad) - kColPad;
        const float maxScroll = std::max(0.f, totalW - (NODE_W - kColPad * 2.f));
        scrollOffset_ = std::clamp(scrollOffset_, 0.f, maxScroll);
        return;
    }
}

// ============================================================================
// Track window (insert list)
// ============================================================================

void InstrumentRackNode::openTrackWindow(int trackID) {
    closeTrackWindow();
    if (!project || !project->processor) return;
    auto* wm = project->processor->getWindowManager();
    if (!wm) return;

    auto win = std::make_unique<InstrumentWindow>(this, trackID);
    InstrumentWindow* ptr = win.get();
    ExpandedWindow* ew = wm->addWindow(std::move(win), 520, 380,
        ("Instrument Rack Track " + std::to_string(trackID)).c_str());
    if (!ew) return;
    ew->show();
    trackWindow_ = ptr;
}

void InstrumentRackNode::selectTrackWindowInsert(int index) {
    if (trackWindow_)
        trackWindow_->setSelectedRow(index);
}

void InstrumentRackNode::closeTrackWindow() {
    if (trackWindow_) {
        trackWindow_->onRequestClose();
        trackWindow_ = nullptr;
    }
}

InstrumentWindow::InstrumentWindow(InstrumentRackNode* node, int trackID)
    : rackNode(node), trackID(trackID) {}

InstrumentWindow::~InstrumentWindow() {
    if (previewTex_) {
        SDL_DestroyTexture(previewTex_);
        previewTex_ = nullptr;
    }
    if (rackNode)
        rackNode->detachTrackWindow(this);
}

void InstrumentRackNode::detachTrackWindow(InstrumentWindow* w) {
    if (trackWindow_ == w)
        trackWindow_ = nullptr;
}

void InstrumentWindow::render() {
    SDL_Renderer* R = renderer;
    if (!R || !rackNode) return;

    SDL_SetRenderDrawColor(R, 40, 40, 45, 255);
    SDL_RenderClear(R);

    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);

    drawText(R, 8.f, 6.f, "Track " + std::to_string(trackID), SDL_Color{230, 230, 230, 255});

    InstrumentRackNode::Track* c = rackNode->trackByID(trackID);
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
                     std::to_string(i + 1) + ": " + insertLabel(c->nodes[i]),
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

void InstrumentWindow::renderPreview(SDL_Renderer* R, Node* sel) {
    // Param textures cache their creating renderer. A new track window has a new renderer,
    // so drop the cached textures when it changes or knobs draw as garbage.
    if (previewRenderer_ != R) {
        previewRenderer_ = R;
        if (previewTex_) {
            SDL_DestroyTexture(previewTex_);
            previewTex_ = nullptr;
        }
        if (rackNode) {
            if (auto* c = rackNode->trackByID(trackID)) {
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
             insertLabel(sel) + " — selected insert", SDL_Color{210, 210, 215, 255});
}

bool InstrumentWindow::handleInput(SDL_Event& e) {
    if (!rackNode) return false;

    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    int wx = 0, wy = 0;
    SDL_GetWindowPosition(window, &wx, &wy);

    float gx = 0.f, gy = 0.f;
    SDL_GetGlobalMouseState(&gx, &gy);
    const float mx = gx - static_cast<float>(wx);
    const float my = gy - static_cast<float>(wy);

    InstrumentRackNode::Track* c = rackNode->trackByID(trackID);
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
                if (rackNode->nm) rackNode->nm->markTopologyDirty();
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
                if (rackNode->nm) rackNode->nm->markTopologyDirty();
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
            del->label = "Remove " + insertLabel(c->nodes[static_cast<size_t>(row)]);
            const int nodeIndex = row;
            del->click = [this, nodeIndex]() {
                for (size_t ci = 0; ci < rackNode->tracks.size(); ++ci)
                    if (rackNode->tracks[ci]->id == trackID) {
                        rackNode->removeNodeFromTrack(static_cast<int>(ci), nodeIndex);
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
                    for (size_t ci = 0; ci < rackNode->tracks.size(); ++ci)
                        if (rackNode->tracks[ci]->id == trackID) {
                            rackNode->addNodeToTrack(static_cast<int>(ci), static_cast<NodeType>(t));
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

json InstrumentRackNode::extraSerialize() {
    json j;
    j["trackIdPool"] = trackIdPool_.toJSON();
    j["nodeIdPool"] = nodeIdPool_.toJSON();
    j["tracks"] = json::array();
    for (auto& cp : tracks) {
        Track& c = *cp;
        json jc;
        jc["id"] = c.id;
        if (c.inflow) jc["inputConnectionID"] = c.inflow->id;
        if (c.outflow) jc["outputConnectionID"] = c.outflow->id;
        jc["pan"] = c.pan.value;
        jc["vol"] = c.volume.value;
        jc["solo"] = c.solo;
        jc["mute"] = c.mute;
        jc["nodes"] = json::array();
        for (Node* n : c.nodes)
            if (n) jc["nodes"].push_back(n->serialize());
        j["tracks"].push_back(std::move(jc));
    }
    return j;
}

void InstrumentRackNode::extraDeSerialize(const json& j) {
    for (auto& cp : tracks) {
        for (Node* n : cp->nodes) destroyNode(n);
        removeInputPort(cp->inflow);
        removeOutputPort(cp->outflow);
    }
    tracks.clear();

    if (j.contains("trackIdPool")) trackIdPool_.fromJSON(j["trackIdPool"]);
    if (j.contains("nodeIdPool")) nodeIdPool_.fromJSON(j["nodeIdPool"]);

    if (j.contains("tracks") && j["tracks"].is_array()) {
        for (const auto& jc : j["tracks"]) {
            auto c = std::make_unique<Track>();
            c->id = jc.value("id", -1);
            if (c->id < 0) c->id = static_cast<int>(trackIdPool_.newID());
            else trackIdPool_.reserveID(static_cast<uint16_t>(c->id));
            c->inflow = addInputPort(static_cast<uint16_t>(jc.value("inputConnectionID", 0)));
            c->outflow = addOutputPort(static_cast<uint16_t>(jc.value("outputConnectionID", 0)));
            c->pan.value = jc.value("pan", 0.5f);
            c->volume.value = jc.value("vol", 1.0f);
            c->solo = jc.value("solo", false);
            c->mute = jc.value("mute", false);
            if (jc.contains("nodes") && jc["nodes"].is_array()) {
                for (const auto& jn : jc["nodes"]) {
                    json jn2;
                    resolveTrackNodeID(nm, jn, jn2);
                    g_vstLoadingChainInsert = true;
                    Node* n = Node::deSerialize(jn2, nm, false);
                    g_vstLoadingChainInsert = false;
                    if (!n) continue;
                    nodeIdPool_.reserveID(n->id);
                    if (nm) nm->registerExtraNode(n);
                    stampTrackOwnership(n, id, c->id);
                    c->nodes.push_back(n);
                }
            }
            tracks.push_back(std::move(c));
        }
    }

    if (tracks.empty()) {
        // Deserialization: direct creation on this graph (no undo, no mirroring).
        addTrackOnGraph(static_cast<int>(trackIdPool_.newID()), 0);
        addTrackOnGraph(static_cast<int>(trackIdPool_.newID()), 0);
    }
    syncTrackParams();
    if (nm) nm->markTopologyDirty();
}
