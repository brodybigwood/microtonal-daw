#include "delay.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include "PatcherExpandedWindow.h"
#include "NodeProcessor.h"
#include "WindowManager.h"
#include "Preferences.h"
#include <algorithm>
#include <cmath>

DelayNode::DelayNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Delay) {
    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);

    params.push_back(&timeKnob);
    params.push_back(&feedbackKnob);
    params.push_back(&mixKnob);

    // Feedback-path graph. Its input is the summed signal, its output is written back into
    // the delay line.
    auto path = nm->managerPath;
    path.push_back(id);
    chain = new NodeManager(project, path);

    // Canvas for the feedback graph, same setup Patcher uses for its inner manager.
    chainEditor = new NodeEditor;
    chainEditor->embedded_ = true;
    chainEditor->setEmbeddedCanvasSize(static_cast<float>(NODE_W), static_cast<float>(NODE_H));
    chain->setNE(chainEditor);
    chainEditor->retach();
}

DelayNode::~DelayNode() {
    if (chainEditor) {
        chainEditor->setTopMenuBarHostNode(nullptr);
        chainEditor->clearWireDragState();
        chainEditor->retach();
        if (chain) chain->resetNE();
        delete chainEditor;
        chainEditor = nullptr;
    }
    delete chain;
    chain = nullptr;
}

void DelayNode::setup() {
    if (!chain || !chain->inNode || !chain->outNode) return;
    // The feedback graph's interface follows the delay's actual channel counts: one inner
    // Input output and one inner Output input per channel of the surrounding signal path.
    const int wantCh = std::max({1, in ? in->numChannels : 1, out ? out->numChannels : 1});
    const size_t want = static_cast<size_t>(wantCh);
    bool changed = false;
    while (chain->inNode->countWaveformOutputs() < want) {
        chain->inNode->addWaveformOutputChannel();
        changed = true;
    }
    while (chain->outNode->countWaveformInputs() < want) {
        chain->outNode->addWaveformInputChannel();
        changed = true;
    }
    if (changed) {
        chain->inNode->makeConnectionRects();
        chain->outNode->makeConnectionRects();
        chain->markTopologyDirty();
    }
}

void DelayNode::ensureCapacity(int channels) {
    const int sr = sampleRate > 0 ? sampleRate : 48000;
    const int wantLen = sr * 8;
    if (lineLen == wantLen && lineChannels == channels) return;
    lineLen = wantLen;
    writePos = 0;
    lineChannels = channels;
    line.assign(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(lineLen), 0.0f));
}

void DelayNode::process() {
    // NodeManager::process takes these by reference, so they cannot be const.
    int bs = bufferSize;
    int sr = sampleRate > 0 ? sampleRate : 48000;
    if (!out || !out->buffer || bs <= 0) return;

    const int ch = std::max(1, out->numChannels);
    ensureCapacity(ch);

    // Time: knob 0..1 maps to 0.01..4 beats, converted with the project tempo at the playhead.
    const float beats = 0.01f + timeKnob[0] * 3.99f;
    float secPerBeat = 0.5f;
    if (project) {
        const float b0 = static_cast<float>(project->dspBeatPosition);
        const float a = project->dspTempoCurve.secondsForBeats(0.f, b0);
        const float b = project->dspTempoCurve.secondsForBeats(0.f, b0 + 1.f);
        if (b > a) secPerBeat = b - a;
    }
    int delay = static_cast<int>(std::lround(static_cast<double>(beats) * secPerBeat * sr));
    delay = std::clamp(delay, 1, lineLen - 1);

    // 100% feedback holds the repeats at the same level instead of capping at 0.95.
    const float fb = std::clamp(feedbackKnob[0], 0.0f, 1.0f);
    const float mix = mixKnob[0];
    const bool haveIn = in && in->is_connected && in->buffer;

    // Read the delayed signal, then form input + previous output * feedback.
    prevBuf.assign(static_cast<size_t>(ch) * bs, 0.0f);
    sumBuf.assign(static_cast<size_t>(ch) * bs, 0.0f);
    for (int c = 0; c < ch; ++c) {
        const int srcCh = haveIn ? std::min(c, std::max(0, in->numChannels - 1)) : 0;
        const float* inCh = haveIn ? in->channel(srcCh) : nullptr;
        float* prev = prevBuf.data() + static_cast<size_t>(c) * bs;
        float* sum = sumBuf.data() + static_cast<size_t>(c) * bs;
        for (int i = 0; i < bs; ++i) {
            int readPos = (writePos + i - delay) % lineLen;
            if (readPos < 0) readPos += lineLen;
            prev[i] = line[static_cast<size_t>(c)][static_cast<size_t>(readPos)];
            sum[i] = (inCh ? inCh[i] : 0.0f) + prev[i] * fb;
        }
    }

    // Feedback graph: its input is the summed signal, its output feeds the line. The graph
    // is the signal path — if nothing is wired into its Output node, there is no wet signal.
    int chainOutCh = ch;
    bool innerWired = false;
    if (chain) {
        for (auto* c : chain->outNode->inputs.connections)
            if (c && c->type == DataType::Waveform && c->is_connected) { innerWired = true; break; }
    }
    if (chain && innerWired) {
        chain->inNode->input = sumBuf.data();
        chain->inNode->numChannels = ch;
        const int innerOut = static_cast<int>(chain->outNode->totalWaveformChannels());
        if (innerOut > 0) chainOutCh = std::min(innerOut, ch);
        chainBuf.assign(static_cast<size_t>(chainOutCh) * bs, 0.0f);
        chain->process(chainBuf.data(), bs, chainOutCh, sr);
    } else {
        chainBuf.assign(static_cast<size_t>(chainOutCh) * bs, 0.0f);
    }

    // The chain output is what goes back into the delay line; the wet signal is the line
    // read, so 100% wet is purely the delayed signal (no dry leakage).
    for (int c = 0; c < ch; ++c) {
        const int srcCh = haveIn ? std::min(c, std::max(0, in->numChannels - 1)) : 0;
        const float* inCh = haveIn ? in->channel(srcCh) : nullptr;
        const float* lineIn = chainBuf.data() + static_cast<size_t>(std::min(c, chainOutCh - 1)) * bs;
        const float* wet = prevBuf.data() + static_cast<size_t>(c) * bs;
        float* outCh = out->channel(c);
        std::vector<float>& buf = line[static_cast<size_t>(c)];
        for (int i = 0; i < bs; ++i) {
            int writeIdx = (writePos + i) % lineLen;
            buf[static_cast<size_t>(writeIdx)] = lineIn[i];
            outCh[i] = (inCh ? inCh[i] : 0.0f) * (1.0f - mix) + wet[i] * mix;
        }
    }
    writePos = (writePos + bs) % lineLen;
}

// "Open" button under the knobs.
static const SDL_FRect kDelayOpenRect{60.f, 78.f, 80.f, 26.f};

void DelayNode::renderContent(SDL_Renderer* renderer) {
    // Base draws the node body/outline and the parameter knobs.
    Node::renderContent(renderer);

    if (fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, "Delay", 0, SDL_Color{200, 200, 200, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_FRect dst{NODE_W * 0.5f - surf->w * 0.5f, 8.0f,
                              static_cast<float>(surf->w), static_cast<float>(surf->h)};
                SDL_RenderTexture(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }

    SDL_SetRenderDrawColor(renderer, 60, 60, 68, 255);
    SDL_RenderFillRect(renderer, &kDelayOpenRect);
    SDL_SetRenderDrawColor(renderer, 100, 100, 110, 255);
    SDL_RenderRect(renderer, &kDelayOpenRect);

    if (fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, "Open", 0, SDL_Color{220, 220, 220, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_FRect dst{kDelayOpenRect.x + kDelayOpenRect.w * 0.5f - surf->w * 0.5f,
                              kDelayOpenRect.y + kDelayOpenRect.h * 0.5f - surf->h * 0.5f,
                              static_cast<float>(surf->w), static_cast<float>(surf->h)};
                SDL_RenderTexture(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }
}

bool DelayNode::handleCustomInput(SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT &&
        msX >= kDelayOpenRect.x && msX < kDelayOpenRect.x + kDelayOpenRect.w &&
        msY >= kDelayOpenRect.y && msY < kDelayOpenRect.y + kDelayOpenRect.h) {
        if (project && project->processor && chainEditor) {
            if (auto* wm = project->processor->getWindowManager()) {
                std::string title = "Delay##" + std::to_string(id);
                if (auto* existing = wm->findByTitle(title.c_str())) {
                    existing->show();
                    SDL_RaiseWindow(existing->window);
                } else {
                    auto pw = std::make_unique<PatcherExpandedWindow>(chainEditor, project);
                    pw->ownerNode = this;
                    if (ExpandedWindow* ew = wm->addWindow(std::move(pw), 1000, 700, title.c_str()))
                        ew->show();
                }
            }
        }
        return true; // only the Open button is consumed here
    }
    return false; // let the base handle dragging/knobs
}

void DelayNode::clearCustomTextures() {}

json DelayNode::extraSerialize() {
    json j;
    if (chain) j["chain"] = chain->serialize();
    return j;
}

void DelayNode::extraDeSerialize(const json& j) {
    if (chain && j.contains("chain")) chain->deSerialize(j["chain"]);
}
