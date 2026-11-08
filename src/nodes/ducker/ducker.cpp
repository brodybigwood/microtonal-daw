#include "ducker.h"
#include "styles.h"
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

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);

    threshold.setValueRange(-60.f, 0.f, 1.f);
    depth.setValueRange(0.f, 1.f, 0.01f);
    attack.setValueRange(0.5f, 200.f, 0.5f);
    release.setValueRange(5.f, 2000.f, 5.f);

    params.push_back(&threshold);
    params.push_back(&depth);
    params.push_back(&attack);
    params.push_back(&release);

    vCount = 4;
    vx = new float[4]{0.f, NODE_W, NODE_W, 0.f};
    vy = new float[4]{0.f, 0.f, kNODE_H, kNODE_H};
    dstRect.w = NODE_W;
    dstRect.h = kNODE_H;
    w = NODE_W;
    h = kNODE_H;
}

void DuckerNode::setup() {
    env = 0.f;
    gainSmoothed = 1.f;
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
    if (!haveAudio) return;

    const float sr = sampleRate > 0 ? static_cast<float>(sampleRate) : 48000.f;
    const float attackMs = std::max(0.5f, attack.value);
    const float releaseMs = std::max(5.f, release.value);
    const float attackCoef = std::exp(-1.f / (attackMs * 0.001f * sr));
    const float releaseCoef = std::exp(-1.f / (releaseMs * 0.001f * sr));
    const float thrLin = std::pow(10.f, threshold.value / 20.f);
    const float duckAmount = std::clamp(depth.value, 0.f, 1.f);

    const int inCh = std::max(1, static_cast<int>(in->numChannels));
    const int keyCh = haveKey ? std::max(1, static_cast<int>(key->numChannels)) : 0;

    for (int i = 0; i < bs; ++i) {
        float lvl = 0.f;
        for (int kc = 0; kc < keyCh; ++kc) {
            const float* kp = key->channel(kc);
            if (kp) lvl = std::max(lvl, std::fabs(kp[i]));
        }
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

    reduction.store(gainSmoothed);
    keyLevel.store(env);
}

void DuckerNode::renderContent(SDL_Renderer* renderer) {
    Node::renderContent(renderer);

    // Real-time meters: key level and the gain reduction actually applied.
    const float sx = 64.f;
    const float sw = NODE_W - 20.f - sx;
    const float level = std::clamp(keyLevel.load(), 0.f, 1.f);
    const float duck = std::clamp(1.f - reduction.load(), 0.f, 1.f);

    auto row = [&](float y, float amount, SDL_Color col) {
        SDL_FRect bg{10.f, y, NODE_W - 20.f, 9.f};
        SDL_SetRenderDrawColor(renderer, 25, 28, 34, 255);
        SDL_RenderFillRect(renderer, &bg);
        SDL_FRect fill{sx, y, sw * amount, 9.f};
        SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 255);
        SDL_RenderFillRect(renderer, &fill);
    };
    auto label = [&](const char* text, float y) {
        if (!fonts.mainFont) return;
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text, 0, SDL_Color{30, 30, 30, 255});
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        if (tex) {
            SDL_FRect tr{10.f, y + 0.5f, static_cast<float>(surf->w), static_cast<float>(surf->h)};
            SDL_RenderTexture(renderer, tex, nullptr, &tr);
            SDL_DestroyTexture(tex);
        }
        SDL_DestroySurface(surf);
    };
    row(116.f, level, SDL_Color{90, 200, 130, 255});
    row(134.f, duck, SDL_Color{230, 140, 80, 255});
    label("KEY", 116.f);
    label("DUCK", 134.f);
}
