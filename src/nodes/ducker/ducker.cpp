#include "ducker.h"
#include "styles.h"
#include "NodeProcessor.h"
#include "Project.h"
#include "NodeManager.h"
#include "nodes/patcher/patcher.h"
#include <algorithm>
#include <cmath>
#include <cstring>

DuckerNode::DuckerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Ducker) {
    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);

    key = new Connection;
    key->type = DataType::Waveform;
    key->dir = Direction::input;
    inputs.addConnection(key);

    // Note trigger: a note-on here adds 1.0 to the key level for that note's
    // duration, so the duck starts on the note's first sample instead of
    // waiting for a sampled kick's attack to cross the threshold.
    events = new Connection;
    events->type = DataType::Events;
    events->dir = Direction::input;
    inputs.addConnection(events);

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);

    threshold.setValueRange(-60.f, 0.f, 1.f);
    depth.setValueRange(0.f, 1.f, 0.01f);
    attack.setValueRange(0.f, 200.f, 0.5f); // 0 = instantaneous duck
    release.setValueRange(5.f, 2000.f, 5.f);

    params.push_back(&threshold);
    params.push_back(&depth);
    params.push_back(&attack);
    params.push_back(&release);

    levelHistory.assign(kHist, 0.f);
    gainHistory.assign(kHist, 1.f);
    guiLevel.assign(kHist, 0.f);
    guiGain.assign(kHist, 1.f);

    // Standard node silhouette (the base trapezoid), stretched to this node's
    // taller height. A plain full-width rectangle covered the title and made
    // the node look unlike every other one.
    vCount = 4;
    vx = new float[4]{0.f, NODE_W, NODE_W - 40.f, 40.f};
    vy = new float[4]{0.f, 0.f, kNODE_H, kNODE_H};
    dstRect.w = NODE_W;
    dstRect.h = kNODE_H;
    w = NODE_W;
    h = kNODE_H;
}

void DuckerNode::setup() {
    env = 0.f;
    gainSmoothed = 1.f;
    eventGate = 0.f;
    heldNotes.clear();
}

void DuckerNode::process() {
    if (!out || !out->buffer) return;

    const int bs = bufferSize;
    const int outCh = std::max(1, static_cast<int>(out->numChannels));
    for (int ch = 0; ch < outCh; ++ch)
        if (float* p = out->channel(ch))
            std::memset(p, 0, static_cast<size_t>(bs) * sizeof(float));

    const bool haveAudio = in && in->is_connected && in->buffer;
    const bool haveKey = key && key->is_connected && key->buffer;
    if (!haveAudio) {
        // Let the display decay and keep the GUI copy fed.
        const auto pushSilent = [this]() {
            dispLevel *= 0.85f;
            levelHistory[histPos] = std::clamp(dispLevel, 0.f, 1.f);
            gainHistory[histPos] = 1.f;
            histPos = (histPos + 1) % kHist;
        };
        pushSilent();
        syncToGui();
        return;
    }

    const float sr = sampleRate > 0 ? static_cast<float>(sampleRate) : 48000.f;
    const float attackMs = std::max(0.f, attack.value);
    const float releaseMs = std::max(5.f, release.value);
    // 0 ms attack means an instantaneous duck (coef 0 => the follower and the
    // gain jump straight to their targets).
    const float attackCoef = attackMs <= 0.f ? 0.f : std::exp(-1.f / (attackMs * 0.001f * sr));
    const float releaseCoef = std::exp(-1.f / (releaseMs * 0.001f * sr));
    const float thrLin = std::pow(10.f, threshold.value / 20.f);
    const float duckAmount = std::clamp(depth.value, 0.f, 1.f);

    const int inCh = std::max(1, static_cast<int>(in->numChannels));
    const int keyCh = haveKey ? std::max(1, static_cast<int>(key->numChannels)) : 0;

    // Event trigger: a note-on adds 1.0 to the key signal for its duration, so
    // the duck starts on the note's own first sample. Applied sample-accurately
    // when the events carry offsets.
    std::vector<float> gateBuf;
    if (events && events->is_connected && events->events && !events->events->empty()) {
        // Track every held note by id and take the max velocity of the held
        // set: a single monophonic value would let one note's note-off cancel
        // the trigger for other notes that are still down (chords / stacked
        // bass), which looked like dropped note-offs.
        struct GateChange {
            int offset = 0;
            bool on = false;
            int id = 0;
            float vel = 0.f;
        };
        std::vector<GateChange> changes;
        changes.reserve(events->events->size());
        for (auto& ev : *events->events) {
            GateChange c;
            c.offset = std::clamp(ev.sampleOffset, 0, bs - 1);
            c.id = ev.id;
            c.on = (ev.type == noteOn);
            c.vel = std::clamp(ev.velocity, 0.f, 1.f);
            changes.push_back(c);
        }
        std::stable_sort(changes.begin(), changes.end(),
                         [](const GateChange& a, const GateChange& b) {
                             return a.offset < b.offset;
                         });

        gateBuf.assign(static_cast<size_t>(bs), 0.f);
        size_t ci = 0;
        auto gateNow = [&]() {
            float m = 0.f;
            for (auto& [id, v] : heldNotes) m = std::max(m, v);
            return m;
        };
        for (int i = 0; i < bs; ++i) {
            while (ci < changes.size() && changes[ci].offset <= i) {
                if (changes[ci].on) heldNotes[changes[ci].id] = changes[ci].vel;
                else heldNotes.erase(changes[ci].id);
                ++ci;
            }
            gateBuf[static_cast<size_t>(i)] = gateNow();
        }
        // Notes still held keep triggering into the next block.
        eventGate = gateNow();
    }

    for (int i = 0; i < bs; ++i) {
        float lvl = 0.f;
        for (int kc = 0; kc < keyCh; ++kc) {
            const float* kp = key->channel(kc);
            if (kp) lvl = std::max(lvl, std::fabs(kp[i]));
        }
        // The trigger is added to the key signal (no separate scaling).
        const float gate = gateBuf.empty() ? eventGate : gateBuf[static_cast<size_t>(i)];
        lvl += gate;
        // Envelope follower: fast attack, slower release.
        env = (lvl > env) ? (lvl + attackCoef * (env - lvl))
                          : (lvl + releaseCoef * (env - lvl));
        const float target = (env > thrLin) ? (1.f - duckAmount) : 1.f;
        const float coef = (target < gainSmoothed) ? attackCoef : releaseCoef;
        gainSmoothed = target + coef * (gainSmoothed - target);

        for (int ch = 0; ch < outCh; ++ch) {
            float* dst = out->channel(ch);
            if (!dst || ch >= inCh) continue;
            const float* src = in->channel(ch);
            if (src) dst[i] = src[i] * gainSmoothed;
        }
    }

    // Display: input amplitude (pre-duck) and the gain curve, one point per block.
    float blockPeak = 0.f;
    for (int c = 0; c < inCh; ++c) {
        const float* src = in->channel(c);
        if (!src) continue;
        for (int i = 0; i < bs; ++i) blockPeak = std::max(blockPeak, std::fabs(src[i]));
    }
    dispLevel = (blockPeak > dispLevel) ? blockPeak : (dispLevel * 0.92f + blockPeak * 0.08f);
    levelHistory[histPos] = std::clamp(dispLevel, 0.f, 1.f);
    gainHistory[histPos] = std::clamp(gainSmoothed, 0.f, 1.f);
    histPos = (histPos + 1) % kHist;
    syncToGui();
}

// Audio thread -> GUI copy, same mechanism VisualizerNode uses. The two graph
// copies own separate node objects, so the values have to be copied across.
void DuckerNode::syncToGui() {
    auto* proj = project;
    if (!proj || !proj->processor) return;
    auto path = nm ? nm->managerPath : std::vector<int>{};
    const auto nodeId = id;
    auto levels = levelHistory;
    auto gains = gainHistory;
    const size_t pos = histPos;
    proj->processor->enqueueProcessorAction([path, nodeId, levels, gains, pos, proj]() mutable {
        NodeManager* mgr = proj->processor->guiGraph;
        for (int patcherId : path) {
            auto* patcher = dynamic_cast<PatcherNode*>(mgr->getNode(static_cast<uint16_t>(patcherId)));
            if (!patcher || !patcher->mainManager) return;
            mgr = patcher->mainManager;
        }
        auto* node = mgr->getNode(static_cast<uint16_t>(nodeId));
        if (auto* d = dynamic_cast<DuckerNode*>(node)) {
            d->guiLevel = std::move(levels);
            d->guiGain = std::move(gains);
            d->guiPos = pos;
        }
    });
}

void DuckerNode::renderContent(SDL_Renderer* renderer) {
    Node::renderContent(renderer);

    // 2D display: input amplitude over time (green) with the duck gain curve
    // (orange) layered on it. The gain is drawn in dB: unity = 0 dB at the top,
    // ducking moves the curve downward.
    const SDL_FRect graph{40.f, 100.f, NODE_W - 80.f, 48.f};
    SDL_SetRenderDrawColor(renderer, 22, 24, 28, 255);
    SDL_RenderFillRect(renderer, &graph);
    SDL_SetRenderDrawColor(renderer, 60, 64, 72, 255);
    SDL_RenderRect(renderer, &graph);

    const size_t n = std::min(guiLevel.size(), static_cast<size_t>(graph.w));
    if (n > 1) {
        const size_t size = guiLevel.size();
        auto yForLevel = [&](float v) {
            return graph.y + graph.h * (1.f - std::clamp(v, 0.f, 1.f));
        };
        auto yForGain = [&](float g) {
            const float db = 20.f * std::log10(std::max(g, 1e-4f));
            const float norm = std::clamp(-db / 48.f, 0.f, 1.f); // 0 dB top, -48 dB bottom
            return graph.y + graph.h * norm;
        };
        for (size_t x = 1; x < n; ++x) {
            const size_t i0 = (guiPos + size - (n - x + 1)) % size;
            const size_t i1 = (guiPos + size - (n - x)) % size;
            const float px0 = graph.x + static_cast<float>(x - 1);
            const float px1 = graph.x + static_cast<float>(x);
            SDL_SetRenderDrawColor(renderer, 90, 200, 130, 255);
            SDL_RenderLine(renderer, px0, yForLevel(guiLevel[i0]), px1, yForLevel(guiLevel[i1]));
            SDL_SetRenderDrawColor(renderer, 230, 140, 80, 255);
            SDL_RenderLine(renderer, px0, yForGain(guiGain[i0]), px1, yForGain(guiGain[i1]));
        }
    }
}
