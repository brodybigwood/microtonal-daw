#include "surroundtostereo.h"
#include "styles.h"
#include "nodes/patcher/patcher.h"
#include "NodeManager.h"
#include "NodeProcessor.h"
#include "Project.h"

static constexpr float DEG2RAD = M_PI / 180.0f;
static constexpr float HEAD_RADIUS_M = 0.0875f;
static constexpr float SPEED_OF_SOUND = 343.0f;

static float itdSeconds(float azimuthDeg) {
    float a = std::abs(azimuthDeg) * DEG2RAD;
    if (a > M_PI) a = 2.0f*M_PI - a;
    return (HEAD_RADIUS_M / SPEED_OF_SOUND) * (a + std::sin(a));
}

static float headShadowDB(float azimuthDeg) {
    float a = std::min(std::abs(azimuthDeg), 180.0f);
    if (a > 90.0f) a = 180.0f - a;
    float t = a / 90.0f;
    return -(2.0f*t + 18.0f*t*t*t);
}

static float headShadowFc(float azimuthDeg, float sampleRate) {
    float a = std::min(std::abs(azimuthDeg), 180.0f);
    if (a > 90.0f) a = 180.0f - a;
    float t = a / 90.0f;
    float fcMin = SPEED_OF_SOUND / (4.0f * HEAD_RADIUS_M);
    float fcNyquist = sampleRate * 0.49f;
    return std::max(fcNyquist - t*(fcNyquist - fcMin), fcMin);
}

static float pinnaConchaFc(float azimuthDeg) {
    float a = std::abs(azimuthDeg);
    if (a > 180.0f) a = 360.0f - a;
    bool rear = a > 90.0f;
    if (rear) a = 180.0f - a;
    float t = a / 90.0f;
    return 4500.0f + t*500.0f - (rear ? 1500.0f : 0.0f);
}

static float pinnaConchaGain(float azimuthDeg) {
    float a = std::abs(azimuthDeg);
    if (a > 180.0f) a = 360.0f - a;
    bool rear = a > 90.0f;
    if (rear) return 0.0f;
    float t = std::min(a, 90.0f) / 90.0f;
    return 8.0f - t*5.0f;
}

static float pinnaNotchFc(float azimuthDeg) {
    float a = std::abs(azimuthDeg);
    if (a > 180.0f) a = 360.0f - a;
    bool rear = a > 90.0f;
    if (rear) a = 180.0f - a;
    float t = a / 90.0f;
    float fc = 7300.0f + t*700.0f;
    if (rear) fc = 10500.0f + t*(-2500.0f);
    return fc;
}

static float earFacing(float sourceDeg, float earDeg) {
    float diff = sourceDeg - earDeg;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    float cosA = std::cos(diff * DEG2RAD);
    return std::max(0.0f, cosA);
}

static float ITD_MAX_SAMPLES(float sr) { return sr * SurroundToStereoNode::ITD_MAX_MS / 1000.0f; }

SurroundToStereoNode::SurroundToStereoNode(uint16_t id, NodeManager* nm)
    : Node(id, nm, NodeType::SurroundToStereo)
{
    const char* labels[6] = {"L", "R", "SL", "SR", "RL", "RR"};
    for (int i = 0; i < 6; ++i) {
        in[i] = new Connection;
        in[i]->type = DataType::Waveform;
        in[i]->dir = Direction::input;
        in[i]->label = labels[i];
        inputs.addConnection(in[i]);
    }
    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    out->numChannels = 2;
    outputs.addConnection(out);
    name = "Surround->Stereo";
}

void SurroundToStereoNode::setup() {
    if (sampleRate <= 0) return;

    float fs = static_cast<float>(sampleRate);
    int newMaxDelay = static_cast<int>(ITD_MAX_SAMPLES(fs)) + 16;
    if (newMaxDelay > maxDelaySamples) {
        for (int i = 0; i < 6; ++i) {
            delete[] ch[i].delayL; ch[i].delayL = nullptr;
            delete[] ch[i].delayR; ch[i].delayR = nullptr;
            ch[i].alloc(newMaxDelay);
        }
        maxDelaySamples = newMaxDelay;
    }

    for (int i = 0; i < 6; ++i) {
        float angle = angles[i];
        bool leftSide = angle < 0;
        float absA = std::abs(angle);

        float itd = itdSeconds(angle);
        int delaySamples = static_cast<int>(std::round(itd * fs));

        auto buildChain = [&](bool isFarEar, float sourceDeg, float earDeg) {
            EarChain ec;
            float facing = earFacing(sourceDeg, earDeg);

            float shadowDB = isFarEar ? headShadowDB(sourceDeg)
                          : ((absA > 45.0f) ? headShadowDB(sourceDeg) * 0.3f : 0.0f);
            float shadowFc = headShadowFc(sourceDeg, fs);
            if (!isFarEar) shadowFc = std::min(shadowFc * 2.0f, fs * 0.49f);
            ec.shadow = Biquad::lowShelf(shadowFc, fs, shadowDB);

            float conchaFc = pinnaConchaFc(sourceDeg);
            float conchaGain = (!isFarEar) ? pinnaConchaGain(sourceDeg) : 0.0f;
            ec.pinna1 = Biquad::peak(conchaFc, fs, conchaGain, 2.5f);

            float notchFc = pinnaNotchFc(sourceDeg);
            float notchDepth = isFarEar ? -14.0f : -(6.0f + (1.0f - facing)*4.0f);
            ec.pinna2 = Biquad::peak(notchFc, fs, notchDepth, 5.0f);

            return ec;
        };

        if (leftSide) {
            ch[i].gainL = 1.0f;
            ch[i].gainR = std::pow(10.0f, headShadowDB(angle)/20.0f);
            ch[i].delaySamplesL = 0;
            ch[i].delaySamplesR = delaySamples;
            ch[i].earL = buildChain(false, angle, -90.0f);
            ch[i].earR = buildChain(true,  angle,  90.0f);
            ch[i].shoulderDelayL = static_cast<int>(1.5f*fs/1000.0f);
            ch[i].shoulderDelayR = 0;
        } else {
            ch[i].gainL = std::pow(10.0f, headShadowDB(angle)/20.0f);
            ch[i].gainR = 1.0f;
            ch[i].delaySamplesL = delaySamples;
            ch[i].delaySamplesR = 0;
            ch[i].earL = buildChain(true,  angle, -90.0f);
            ch[i].earR = buildChain(false, angle,  90.0f);
            ch[i].shoulderDelayL = 0;
            ch[i].shoulderDelayR = static_cast<int>(1.5f*fs/1000.0f);
        }

        ch[i].delaySamplesL = std::min(ch[i].delaySamplesL, maxDelaySamples-1);
        ch[i].delaySamplesR = std::min(ch[i].delaySamplesR, maxDelaySamples-1);
        ch[i].shoulderDelayL = std::min(ch[i].shoulderDelayL, maxDelaySamples-1);
        ch[i].shoulderDelayR = std::min(ch[i].shoulderDelayR, maxDelaySamples-1);
    }
}

void SurroundToStereoNode::process() {
    if (!out || !out->buffer || bufferSize <= 0) return;

    float* outL = out->channel(0);
    float* outR = out->channel(1);
    float peakL = 0.0f, peakR = 0.0f;
    std::memset(outL, 0, bufferSize * sizeof(float));
    std::memset(outR, 0, bufferSize * sizeof(float));

    for (int ci = 0; ci < 6; ++ci) {
        Connection* c = in[ci];
        if (!c || !c->is_connected || !c->buffer) continue;

        auto& sc = ch[ci];
        float* src = c->channel(0);
        float peak = 0.0f;

        for (int i = 0; i < bufferSize; ++i) {
            float x = src[i];

            sc.delayL[sc.writePos] = x;
            sc.delayR[sc.writePos] = x;

            int w = sc.writePos;

            int rpL = w - sc.delaySamplesL;
            if (rpL < 0) rpL += sc.delayLen;
            float sigL = sc.delayL[rpL] * sc.gainL;
            int rpLs = w - sc.shoulderDelayL;
            if (rpLs < 0) rpLs += sc.delayLen;
            sigL += sc.delayL[rpLs] * sc.gainL * 0.15f;

            int rpR = w - sc.delaySamplesR;
            if (rpR < 0) rpR += sc.delayLen;
            float sigR = sc.delayR[rpR] * sc.gainR;
            int rpRs = w - sc.shoulderDelayR;
            if (rpRs < 0) rpRs += sc.delayLen;
            sigR += sc.delayR[rpRs] * sc.gainR * 0.15f;

            sigL = sc.earL.pinna2.process(sc.earL.pinna1.process(sc.earL.shadow.process(sigL)));
            sigR = sc.earR.pinna2.process(sc.earR.pinna1.process(sc.earR.shadow.process(sigR)));

            outL[i] += sigL;
            outR[i] += sigR;

            float a = std::abs(x);
            if (a > peak) peak = a;

            sc.writePos = (w + 1) % sc.delayLen;
        }
        peakAmps[ci] = peakAmps[ci] * 0.95f + peak * 0.05f;
    }
    for (int i = 0; i < bufferSize; ++i) {
        if (std::abs(outL[i]) > peakL) peakL = std::abs(outL[i]);
        if (std::abs(outR[i]) > peakR) peakR = std::abs(outR[i]);
    }
    outPeak = outPeak * 0.92f + std::max(peakL, peakR) * 0.08f;
    syncToGui();
}

void SurroundToStereoNode::syncToGui() {
    auto mgrPath = nm->managerPath;
    auto nodeId = id;
    float amps[6];
    for (int i = 0; i < 6; ++i) amps[i] = peakAmps[i];
    float op = outPeak;
    auto* proj = project;
    if (!proj || !proj->processor) return;
    proj->processor->enqueueProcessorAction([mgrPath, nodeId, amps, op, proj]() mutable {
        NodeManager* mgr = proj->processor->guiManager;
        for (int patcherId : mgrPath) {
            auto* patcher = dynamic_cast<PatcherNode*>(mgr->getNode(static_cast<uint16_t>(patcherId)));
            if (!patcher || !patcher->mainManager) return;
            mgr = patcher->mainManager;
        }
        auto* node = mgr->getNode(static_cast<uint16_t>(nodeId));
        auto* sts = dynamic_cast<SurroundToStereoNode*>(node);
        if (sts) {
            for (int i = 0; i < 6; ++i) sts->peakAmpsGui_[i] = amps[i];
            sts->outPeakGui_ = op;
        }
    });
}

void SurroundToStereoNode::renderContent(SDL_Renderer* renderer) {
    // Hexagon window shape
    float cx = NODE_W * 0.5f;
    float cy = NODE_H * 0.5f;
    float r = std::min(NODE_W, NODE_H) * 0.48f;

    if (vCount != 6) {
        vCount = 6;
        delete[] vx; delete[] vy;
        vx = new float[6];
        vy = new float[6];
    }
    for (int i = 0; i < 6; ++i) {
        float a = (60.0f * static_cast<float>(i) - 30.0f) * DEG2RAD; // flat-top hexagon
        vx[i] = cx + r * std::cos(a);
        vy[i] = cy + r * std::sin(a);
    }

    filledPolygonRGBA(renderer, vx, vy, 6, 36, 38, 40, 255);
    aapolygonRGBA(renderer, vx, vy, 6, 80, 80, 82, 255);

    // Inner hex for speakers
    float ir = r * 0.72f;
    float hx[6], hy[6];
    for (int i = 0; i < 6; ++i) {
        float a = angles[i] * DEG2RAD - M_PI * 0.5f;
        hx[i] = cx + ir * std::cos(a);
        hy[i] = cy + ir * std::sin(a);
    }

    aapolygonRGBA(renderer, hx, hy, 6, 50, 50, 52, 180);

    for (int i = 0; i < 6; ++i) {
        float a = angles[i] * DEG2RAD - M_PI * 0.5f;
        float sx = cx + ir * std::cos(a);
        float sy = cy + ir * std::sin(a);

        float ampR = 5.0f + peakAmpsGui_[i] * 22.0f;
        float ac = std::clamp(peakAmpsGui_[i], 0.0f, 1.0f);
        uint8_t red = static_cast<uint8_t>(40 + ac * 215);
        uint8_t grn = static_cast<uint8_t>(180 - ac * 140);
        filledCircleRGBA(renderer, static_cast<int>(sx), static_cast<int>(sy),
                         static_cast<int>(ampR), red, grn, 60, 255);

        filledCircleRGBA(renderer, static_cast<int>(sx), static_cast<int>(sy), 5, 200, 200, 200, 255);
    }

    // Listener — size based on stereo output level
    float outPeak = (peakAmpsGui_[0] + peakAmpsGui_[1]) * 0.5f;
    float lr = 6.0f + outPeak * 20.0f;
    float lac = std::clamp(outPeak, 0.0f, 1.0f);
    uint8_t lr8 = static_cast<uint8_t>(40 + lac * 215);
    uint8_t lg8 = static_cast<uint8_t>(180 - lac * 140);
    filledCircleRGBA(renderer, static_cast<int>(cx), static_cast<int>(cy),
                     static_cast<int>(lr), lr8, lg8, 60, 255);
    filledCircleRGBA(renderer, static_cast<int>(cx), static_cast<int>(cy), 4, 40, 40, 42, 255);

    for (int i = 0; i < 6; ++i) {
        float a = angles[i] * DEG2RAD - M_PI * 0.5f;
        float sx = cx + ir * std::cos(a);
        float sy = cy + ir * std::sin(a);
        aalineRGBA(renderer, static_cast<int>(cx), static_cast<int>(cy),
                   static_cast<int>(sx), static_cast<int>(sy), 50, 50, 52, 120);
    }

    const char* labels[6] = {"L", "R", "SL", "SR", "RL", "RR"};
    for (int i = 0; i < 6; ++i) {
        if (!fonts.mainFont) continue;
        float a = angles[i] * DEG2RAD - M_PI * 0.5f;
        float lx = cx + (ir + 14.0f) * std::cos(a);
        float ly = cy + (ir + 14.0f) * std::sin(a);
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, labels[i], 0, SDL_Color{180, 180, 180, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_FRect tr{lx - surf->w * 0.5f, ly - surf->h * 0.5f,
                             static_cast<float>(surf->w), static_cast<float>(surf->h)};
                SDL_RenderTexture(renderer, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }
}
