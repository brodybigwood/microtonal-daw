#include "surroundtostereo.h"
#include <cmath>
#include <cstring>
#include <algorithm>

static constexpr float DEG2RAD = M_PI / 180.0f;
static constexpr float HEAD_RADIUS_M = 0.0875f;
static constexpr float SPEED_OF_SOUND = 343.0f;

// ---- ITD from Woodworth formula ----
static float itdSeconds(float azimuthDeg) {
    float a = std::abs(azimuthDeg) * DEG2RAD;
    if (a > M_PI) a = 2.0f*M_PI - a;
    return (HEAD_RADIUS_M / SPEED_OF_SOUND) * (a + std::sin(a));
}

// ---- Head shadow ILD (dB, negative = attenuation) ----
// Based on spherical head model: ~0dB at 0°, dropping to ~-20dB at 90°
static float headShadowDB(float azimuthDeg) {
    float a = std::min(std::abs(azimuthDeg), 180.0f);
    if (a > 90.0f) a = 180.0f - a; // rear ~= front for head shadow
    float t = a / 90.0f;
    return -(2.0f*t + 18.0f*t*t*t); // approx fit to KEMAR data
}

// ---- Head shadow cutoff frequency (for shelf filter) ----
// Sphere diffraction: fc drops with angle. At 0°: no shadow (very high fc).
// At 90°: fc ~= c/(4a) ≈ 980 Hz
static float headShadowFc(float azimuthDeg, float sampleRate) {
    float a = std::min(std::abs(azimuthDeg), 180.0f);
    if (a > 90.0f) a = 180.0f - a;
    float t = a / 90.0f;
    float fcMin = SPEED_OF_SOUND / (4.0f * HEAD_RADIUS_M); // ~980 Hz
    float fcNyquist = sampleRate * 0.49f;
    float fc = fcNyquist - t*(fcNyquist - fcMin);
    return std::max(fc, fcMin);
}

// ---- Pinna notch frequency shifts with source angle ----
// Front:  concha boost ~4.5kHz, notch ~7.3kHz
// Side:   concha boost ~5.0kHz, notch ~8.0kHz
// Rear:   concha boost ~3.5kHz, notch ~10.5kHz (no boost when behind)
static float pinnaConchaFc(float azimuthDeg) {
    float a = std::abs(azimuthDeg);
    if (a > 180.0f) a = 360.0f - a;
    bool rear = a > 90.0f;
    if (rear) a = 180.0f - a;
    float t = a / 90.0f;
    // Front: 4500, Side: 5000, Rear: 3500
    return 4500.0f + t*500.0f - (rear ? 1500.0f : 0.0f);
}

static float pinnaConchaGain(float azimuthDeg) {
    float a = std::abs(azimuthDeg);
    if (a > 180.0f) a = 360.0f - a;
    bool rear = a > 90.0f;
    // Front: +8dB boost, Side: +3dB, Rear: 0dB
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
    // Front: 7300, Side: 8000, Rear: 10500
    float fc = 7300.0f + t*700.0f;
    if (rear) fc = 10500.0f + t*(-2500.0f); // rear to side transition
    return fc;
}

// ---- Directionality index for the ear (how much the ear faces the source) ----
// Returns 0..1 where 1 = source directly facing this ear
static float earFacing(float sourceDeg, float earDeg) {
    float diff = sourceDeg - earDeg;
    // Normalize to [-180, 180]
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    // Cardioid-like: cos^2 of half-angle, strongest at 0°, weakest at 180°
    float cosA = std::cos(diff * DEG2RAD);
    return std::max(0.0f, cosA); // linear, 1=facing, 0=opposite
}

static float ITD_MAX_SAMPLES(float sr) { return sr * SurroundToStereoNode::ITD_MAX_MS / 1000.0f; }

// ============================================================================
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
    params.push_back(&mix);
    params.push_back(&room);
    name = "Surround->Stereo";
}

// ============================================================================
void SurroundToStereoNode::setup() {
    if (sampleRate <= 0) return;

    float fs = static_cast<float>(sampleRate);
    int newMaxDelay = static_cast<int>(ITD_MAX_SAMPLES(fs)) + static_cast<int>(room.value*25.0f*fs/1000.0f) + 60;
    if (newMaxDelay > maxDelaySamples) {
        for (int i = 0; i < 6; ++i) {
            delete[] ch[i].delayL; ch[i].delayL = nullptr;
            delete[] ch[i].delayR; ch[i].delayR = nullptr;
            ch[i].alloc(newMaxDelay);
        }
        maxDelaySamples = newMaxDelay;
    }

    for (int i = 0; i < 6; ++i) {
        float angle = angles[i]; // e.g. -30, 30, -90, 90, -150, 150
        bool leftSide = angle < 0;
        float absA = std::abs(angle);

        float itd = itdSeconds(angle);
        int delaySamples = static_cast<int>(std::round(itd * fs));

        // Rear pre-delay (room simulation, scaled by Room knob)
        int preDelay = 0;
        if (absA > 120.0f) {
            preDelay = static_cast<int>(room.value * 20.0f * fs / 1000.0f);
        } else if (absA > 60.0f) {
            preDelay = static_cast<int>(room.value * 3.0f * fs / 1000.0f);
        }

        // ---- Build ear chains for this source angle ----
        // For the ear OPPOSITE the source (far ear): full head shadow + pinna
        // For the ear FACING the source (near ear): milder processing

        auto buildChain = [&](bool isFarEar, float sourceDeg, float earDeg) {
            EarChain ec;
            float facing = earFacing(sourceDeg, earDeg);

            if (isFarEar) {
                // Far ear: full head shadow
                float shadowDB = headShadowDB(sourceDeg);
                float shadowFc = headShadowFc(sourceDeg, fs);
                ec.shadow = Biquad::lowShelf(shadowFc, fs, shadowDB);
            } else {
                // Near ear: mild shelf for very lateral sources
                float mildDB = (absA > 45.0f) ? headShadowDB(sourceDeg) * 0.3f : 0.0f;
                if (mildDB < -0.5f)
                    ec.shadow = Biquad::lowShelf(headShadowFc(sourceDeg, fs)*2.0f, fs, mildDB);
            }

            // Pinna resonance (concha): only for IEM simulation, always present
            float conchaFc = pinnaConchaFc(sourceDeg);
            float conchaGain = pinnaConchaGain(sourceDeg);
            // Near ear gets full concha; far ear gets reduced
            if (!isFarEar && conchaGain > 1.0f)
                ec.pinna1 = Biquad::peak(conchaFc, fs, conchaGain, 2.5f);

            // Pinna notch: shifts with angle, simulates directional filtering
            float notchFc = pinnaNotchFc(sourceDeg);
            // Far ear: deeper notch. Near ear: shallower
            float notchDepth = isFarEar ? -14.0f : -(6.0f + (1.0f - facing)*4.0f);
            ec.pinna2 = Biquad::peak(notchFc, fs, notchDepth, 5.0f);

            // Shoulder reflection: delayed tap ~1.5ms (stronger for front, weaker side)
            ec.shouldTap = isFarEar ? 0.0f : (0.06f + 0.14f * facing);

            return ec;
        };

        if (leftSide) {
            ch[i].gainL = 1.0f;
            ch[i].gainR = std::pow(10.0f, headShadowDB(angle)/20.0f);
            ch[i].delaySamplesL = preDelay;
            ch[i].delaySamplesR = delaySamples + preDelay;
            ch[i].earL = buildChain(false, angle, -90.0f); // left ear = near
            ch[i].earR = buildChain(true,  angle,  90.0f); // right ear = far
            ch[i].shoulderDelayL = static_cast<int>(1.5f*fs/1000.0f) + preDelay;
            ch[i].shoulderDelayR = 0;
        } else {
            ch[i].gainL = std::pow(10.0f, headShadowDB(angle)/20.0f);
            ch[i].gainR = 1.0f;
            ch[i].delaySamplesL = delaySamples + preDelay;
            ch[i].delaySamplesR = preDelay;
            ch[i].earL = buildChain(true,  angle, -90.0f); // left ear = far
            ch[i].earR = buildChain(false, angle,  90.0f); // right ear = near
            ch[i].shoulderDelayL = 0;
            ch[i].shoulderDelayR = static_cast<int>(1.5f*fs/1000.0f) + preDelay;
        }

        // Clamp
        ch[i].delaySamplesL = std::min(ch[i].delaySamplesL, maxDelaySamples-1);
        ch[i].delaySamplesR = std::min(ch[i].delaySamplesR, maxDelaySamples-1);
        ch[i].shoulderDelayL = std::min(ch[i].shoulderDelayL, maxDelaySamples-1);
        ch[i].shoulderDelayR = std::min(ch[i].shoulderDelayR, maxDelaySamples-1);
    }
}

// ============================================================================
void SurroundToStereoNode::process() {
    if (!out || !out->buffer || bufferSize <= 0) return;

    float* outL = out->channel(0);
    float* outR = out->channel(1);
    std::memset(outL, 0, bufferSize * sizeof(float));
    std::memset(outR, 0, bufferSize * sizeof(float));

    float m = std::clamp(mix.value, 0.0f, 1.0f);
    float dry = 1.0f - m;

    for (int ci = 0; ci < 6; ++ci) {
        Connection* c = in[ci];
        if (!c || !c->is_connected || !c->buffer) continue;

        auto& sc = ch[ci];
        float* src = c->channel(0);

        for (int i = 0; i < bufferSize; ++i) {
            float x = src[i];

            // Write into delay lines
            sc.delayL[sc.writePos] = x;
            sc.delayR[sc.writePos] = x;

            int w = sc.writePos;

            // Left ear: read primary + shoulder tap
            int rpL = w - sc.delaySamplesL;
            if (rpL < 0) rpL += sc.delayLen;
            float sigL = sc.delayL[rpL] * sc.gainL;
            int rpLs = w - sc.shoulderDelayL;
            if (rpLs < 0) rpLs += sc.delayLen;
            sigL += sc.delayL[rpLs] * sc.gainL * sc.earL.shouldTap;

            // Right ear
            int rpR = w - sc.delaySamplesR;
            if (rpR < 0) rpR += sc.delayLen;
            float sigR = sc.delayR[rpR] * sc.gainR;
            int rpRs = w - sc.shoulderDelayR;
            if (rpRs < 0) rpRs += sc.delayLen;
            sigR += sc.delayR[rpRs] * sc.gainR * sc.earR.shouldTap;

            // Run biquad chains
            sigL = sc.earL.pinna2.process(sc.earL.pinna1.process(sc.earL.shadow.process(sigL)));
            sigR = sc.earR.pinna2.process(sc.earR.pinna1.process(sc.earR.shadow.process(sigR)));

            // Dry passthrough for front L/R channels only
            float dryL = (ci == 0) ? x * dry : 0;
            float dryR = (ci == 1) ? x * dry : 0;

            outL[i] += sigL * m + dryL;
            outR[i] += sigR * m + dryR;

            sc.writePos = (w + 1) % sc.delayLen;
        }
    }
}

// ============================================================================
void SurroundToStereoNode::renderContent(SDL_Renderer* renderer) {
    Node::renderContent(renderer);
}
