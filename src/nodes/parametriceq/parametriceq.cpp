#include "parametriceq.h"
#include "AudioManager.h"
#include "NodeManager.h"
#include "NodeProcessor.h"
#include "Project.h"
#include "styles.h"
#include "UndoManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// --- EQBand ---

EQBand::EQBand(float typeVal, float freqX, float freqY, float freqR,
               float gainX, float gainY, float gainR,
               float qX, float qY, float qR,
               float dropX, float dropY, float dropW, float dropH)
    : type(DropdownParameter(typeVal, dropX, dropY, dropW, dropH,
           {"Peaking", "Low Shelf", "High Shelf", "Low Pass", "High Pass", "Band Pass", "Notch"}))
    , frequency(Knob(0.5f, freqX, freqY, freqR, "assets/knobs/1.png", -135.0f, 135.0f, "Freq", {220, 220, 220, 255}))
    , gain(Knob(0.5f, gainX, gainY, gainR, "assets/knobs/1.png", -135.0f, 135.0f, "Gain", {220, 220, 220, 255}))
    , q(Knob(0.1f, qX, qY, qR, "assets/knobs/1.png", -135.0f, 135.0f, "Q", {220, 220, 220, 255}))
{
    frequency.value = 0.5f;
    gain.value = 0.5f;
    q.value = 0.1f;
}

void EQBand::computeCoefficients(float freqHz, float gainDb, float qVal, int typeIdx, int sr) {
    if (sr <= 0) return;

    constexpr float kPi = 3.14159265358979323846f;
    float A = std::pow(10.0f, gainDb / 40.0f);
    float w0 = 2.0f * kPi * freqHz / static_cast<float>(sr);
    float cosW = std::cos(w0);
    float sinW = std::sin(w0);
    float alpha = (qVal > 0.001f) ? sinW / (2.0f * qVal) : std::sin(w0) * 0.5f;

    float a0 = 1.0f;

    switch (typeIdx) {
        case 0: { // Peaking
            float alphaA = alpha * A;
            float alphaDivA = alpha / A;
            b0 =  1.0f + alphaA;
            b1 = -2.0f * cosW;
            b2 =  1.0f - alphaA;
            a1 = -2.0f * cosW;
            a2 =  1.0f - alphaDivA;
            a0 =  1.0f + alphaDivA;
            break;
        }
        case 1: { // LowShelf
            float sqrtA = std::sqrt(A);
            float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
            b0 =       A * ((A + 1.0f) - (A - 1.0f) * cosW + twoSqrtAAlpha);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW);
            b2 =       A * ((A + 1.0f) - (A - 1.0f) * cosW - twoSqrtAAlpha);
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW);
            a2 =        (A + 1.0f) + (A - 1.0f) * cosW - twoSqrtAAlpha;
            a0 =        (A + 1.0f) + (A - 1.0f) * cosW + twoSqrtAAlpha;
            break;
        }
        case 2: { // HighShelf
            float sqrtA = std::sqrt(A);
            float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
            b0 =       A * ((A + 1.0f) + (A - 1.0f) * cosW + twoSqrtAAlpha);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW);
            b2 =       A * ((A + 1.0f) + (A - 1.0f) * cosW - twoSqrtAAlpha);
            a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cosW);
            a2 =        (A + 1.0f) - (A - 1.0f) * cosW - twoSqrtAAlpha;
            a0 =        (A + 1.0f) - (A - 1.0f) * cosW + twoSqrtAAlpha;
            break;
        }
        case 3: { // LowPass
            b0 = (1.0f - cosW) * 0.5f;
            b1 = 1.0f - cosW;
            b2 = (1.0f - cosW) * 0.5f;
            a1 = -2.0f * cosW;
            a2 = 1.0f - alpha;
            a0 = 1.0f + alpha;
            break;
        }
        case 4: { // HighPass
            b0 =  (1.0f + cosW) * 0.5f;
            b1 = -(1.0f + cosW);
            b2 =  (1.0f + cosW) * 0.5f;
            a1 = -2.0f * cosW;
            a2 = 1.0f - alpha;
            a0 = 1.0f + alpha;
            break;
        }
        case 5: { // BandPass
            b0 =  alpha;
            b1 =  0.0f;
            b2 = -alpha;
            a1 = -2.0f * cosW;
            a2 = 1.0f - alpha;
            a0 = 1.0f + alpha;
            break;
        }
        case 6: { // Notch
            b0 =  1.0f;
            b1 = -2.0f * cosW;
            b2 =  1.0f;
            a1 = -2.0f * cosW;
            a2 = 1.0f - alpha;
            a0 = 1.0f + alpha;
            break;
        }
        default: return;
    }

    float invA0 = 1.0f / a0;
    b0 *= invA0; b1 *= invA0; b2 *= invA0;
    a1 *= invA0; a2 *= invA0;

    lastFreqHz = freqHz;
    lastGainDb = gainDb;
    lastQ = qVal;
    lastTypeIdx = static_cast<size_t>(typeIdx);
    coeffDirty = false;
}

float EQBand::processSample(float x) {
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
}

void EQBand::reset() {
    x1 = x2 = y1 = y2 = 0.0f;
}

// --- ParametricEQNode ---

ParametricEQNode::ParametricEQNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::ParametricEQ) {
    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);
}

ParametricEQNode::~ParametricEQNode() = default;

float ParametricEQNode::mapFreq(float norm) const {
    float lo = std::log(freqMin);
    float hi = std::log(freqMax);
    return std::exp(lo + std::clamp(norm, 0.0f, 1.0f) * (hi - lo));
}

float ParametricEQNode::mapGainDb(float norm) const {
    return gainDbMin + std::clamp(norm, 0.0f, 1.0f) * (gainDbMax - gainDbMin);
}

float ParametricEQNode::mapQ(float norm) const {
    return qMin + std::clamp(norm, 0.0f, 1.0f) * (qMax - qMin);
}

void ParametricEQNode::updateBandPositions() {
    int n = static_cast<int>(bands.size());

    float totalW = graphRect.w;
    float colW = n > 0 ? std::min(150.0f, totalW / static_cast<float>(n)) : 150.0f;
    float startX = n > 0 ? graphRect.x + (totalW - colW * static_cast<float>(n)) * 0.5f : graphRect.x;

    for (int i = 0; i < n; i++) {
        float cx = startX + colW * (static_cast<float>(i) + 0.5f);
        auto& b = *bands[i];

        b.type.reposition(cx - 60.0f, 388.0f, 120.0f, 24.0f);
        b.frequency.reposition(cx, 442.0f, 16.0f);
        b.gain.reposition(cx, 500.0f, 16.0f);
        b.q.reposition(cx, 558.0f, 16.0f);
        b.removeRect = {cx - 10.0f, 584.0f, 20.0f, 20.0f};
    }

    buildBandParams();
}

void ParametricEQNode::addBand(int insertAt) {
    int idx = (insertAt < 0 || insertAt > static_cast<int>(bands.size()))
                  ? static_cast<int>(bands.size()) : insertAt;

    bands.insert(bands.begin() + idx,
                 std::make_unique<EQBand>(0.0f, 0, 0, 16.0f, 0, 0, 16.0f, 0, 0, 16.0f, 0, 0, 120.0f, 24.0f));

    updateBandPositions();
}

void ParametricEQNode::removeBand(int index) {
    if (bands.empty() || index < 0 || index >= static_cast<int>(bands.size())) return;
    bands.erase(bands.begin() + index);

    updateBandPositions();
}

void ParametricEQNode::buildBandParams() {
    params.clear();
    for (auto& band : bands) {
        params.push_back(&band->type);
        params.push_back(&band->frequency);
        params.push_back(&band->gain);
        params.push_back(&band->q);
    }
}

void ParametricEQNode::setup() {
    for (auto& band : bands) band->reset();
    if (!bands.empty()) {
        int sr = sampleRate;
        for (auto& band : bands) band->computeCoefficients(
            mapFreq(band->frequency.value), mapGainDb(band->gain.value),
            mapQ(band->q.value), static_cast<int>(band->type.getChoiceIndex()), sr);
    }
}

void ParametricEQNode::process() {
    if (!out || !out->buffer) return;

    if (!in || !in->is_connected || !in->buffer) {
        std::memset(out->buffer, 0, static_cast<size_t>(bufferSize) * sizeof(float));
        return;
    }

    for (int i = 0; i < bufferSize; ++i) {
        float dry = in->buffer[i];
        float y = dry;

        for (auto& band : bands) {
            float freqHz = mapFreq(band->frequency[i]);
            float gainDb = mapGainDb(band->gain[i]);
            float qVal = mapQ(band->q[i]);
            int typeIdx = static_cast<int>(std::round(band->type[i] * static_cast<float>(band->type.choices.size() - 1)));

            if (freqHz != band->lastFreqHz || gainDb != band->lastGainDb ||
                qVal != band->lastQ || static_cast<size_t>(typeIdx) != band->lastTypeIdx || band->coeffDirty) {
                band->computeCoefficients(freqHz, gainDb, qVal, typeIdx, sampleRate);
            }
            y = band->processSample(y);
        }

        out->buffer[i] = y;
        dryRing[ringWrite] = dry;
        wetRing[ringWrite] = y;
        ringWrite = (ringWrite + 1) % fftSize;
    }

    syncToGui();
}

void ParametricEQNode::syncToGui() {
    auto path = nm->managerPath;
    auto nodeId = id;
    auto dryRingCopy = dryRing;
    auto wetRingCopy = wetRing;
    auto rw = ringWrite;
    auto* proj = project;
    if (!proj || !proj->processor) return;
    proj->processor->enqueueProcessorAction([path, nodeId, dryRingCopy, wetRingCopy, rw, proj]() mutable {
        NodeManager* mgr = proj->processor->guiManager;
        for (int patcherId : path) {
            auto* patcher = dynamic_cast<PatcherNode*>(mgr->getNode(static_cast<uint16_t>(patcherId)));
            if (!patcher || !patcher->mainManager) return;
            mgr = patcher->mainManager;
        }
        auto* node = mgr->getNode(static_cast<uint16_t>(nodeId));
        auto* eq = dynamic_cast<ParametricEQNode*>(node);
        if (eq) {
            eq->dryRing = dryRingCopy;
            eq->wetRing = wetRingCopy;
            eq->ringWrite = rw;
        }
    });
}

// --- Frequency Response ---

void ParametricEQNode::computeResponseCurve(std::vector<SDL_FPoint>& pts) const {
    pts.clear();
    if (bands.empty()) return;
    int sr = AudioManager::instance()->sampleRate;
    if (sr <= 0) sr = 48000;

    const int numPts = 300;
    pts.reserve(numPts);

    float logMin = std::log(freqMin);
    float logMax = std::log(freqMax);

    for (int i = 0; i < numPts; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(numPts - 1);
        float freqHz = std::exp(logMin + t * (logMax - logMin));

        float magDb = 0.0f;
        constexpr float k2Pi = 2.0f * 3.14159265358979323846f;

        for (auto& band : bands) {
            float fHz = mapFreq(band->frequency.value);
            float gDb = mapGainDb(band->gain.value);
            float qVal = mapQ(band->q.value);
            int tIdx = static_cast<int>(band->type.getChoiceIndex());

            float A = std::pow(10.0f, gDb / 40.0f);
            float w0 = k2Pi * fHz / static_cast<float>(sr);
            float cosWc = std::cos(w0);
            float sinWc = std::sin(w0);
            float alpha = (qVal > 0.001f) ? sinWc / (2.0f * qVal) : sinWc * 0.5f;

            float b0, b1, b2, a0, a1, a2;
            a0 = 1.0f;

            switch (tIdx) {
                case 0: { b0 = 1.0f + alpha*A; b1 = -2.0f*cosWc; b2 = 1.0f - alpha*A; a1 = -2.0f*cosWc; a2 = 1.0f - alpha/A; a0 = 1.0f + alpha/A; break; }
                case 1: { float sa=std::sqrt(A), ts=2.0f*sa*alpha; b0=A*((A+1)-(A-1)*cosWc+ts); b1=2*A*((A-1)-(A+1)*cosWc); b2=A*((A+1)-(A-1)*cosWc-ts); a1=-2*((A-1)+(A+1)*cosWc); a2=(A+1)+(A-1)*cosWc-ts; a0=(A+1)+(A-1)*cosWc+ts; break; }
                case 2: { float sa=std::sqrt(A), ts=2.0f*sa*alpha; b0=A*((A+1)+(A-1)*cosWc+ts); b1=-2*A*((A-1)+(A+1)*cosWc); b2=A*((A+1)+(A-1)*cosWc-ts); a1=2*((A-1)-(A+1)*cosWc); a2=(A+1)-(A-1)*cosWc-ts; a0=(A+1)-(A-1)*cosWc+ts; break; }
                case 3: { b0=(1-cosWc)*0.5f; b1=1-cosWc; b2=(1-cosWc)*0.5f; a1=-2*cosWc; a2=1-alpha; a0=1+alpha; break; }
                case 4: { b0=(1+cosWc)*0.5f; b1=-(1+cosWc); b2=(1+cosWc)*0.5f; a1=-2*cosWc; a2=1-alpha; a0=1+alpha; break; }
                case 5: { b0=alpha; b1=0; b2=-alpha; a1=-2*cosWc; a2=1-alpha; a0=1+alpha; break; }
                case 6: { b0=1; b1=-2*cosWc; b2=1; a1=-2*cosWc; a2=1-alpha; a0=1+alpha; break; }
                default: continue;
            }

            float w = k2Pi * freqHz / static_cast<float>(sr);
            float cosW2 = std::cos(w);
            float cos2W = std::cos(2.0f * w);
            float sinW2 = std::sin(w);
            float sin2W = std::sin(2.0f * w);

            float numRe = b0 + b1 * cosW2 + b2 * cos2W;
            float numIm = b1 * sinW2 + b2 * sin2W;
            float denRe = a0 + a1 * cosW2 + a2 * cos2W;
            float denIm = a1 * sinW2 + a2 * sin2W;

            float mag = std::sqrt(numRe * numRe + numIm * numIm) /
                        std::sqrt(std::max(denRe * denRe + denIm * denIm, 1e-20f));
            magDb += 20.0f * std::log10(std::max(mag, 1e-10f));
        }

        float x = graphRect.x + t * graphRect.w;
        float normGain = (magDb - gainDbMin) / (gainDbMax - gainDbMin);
        float y = graphRect.y + graphRect.h * (1.0f - std::clamp(normGain, 0.0f, 1.0f));
        pts.push_back({x, y});
    }
}

// --- Rendering ---

void ParametricEQNode::renderContent(SDL_Renderer* renderer) {
    updateBandPositions();

    if (!vCount) {
        vCount = 4;
        vx = new float[4]{0, NODE_W, NODE_W, 0};
        vy = new float[4]{0, 0, NODE_H, NODE_H};
    }
    SDL_FRect fullRect{0, 0, NODE_W, NODE_H};
    SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
    SDL_RenderFillRect(renderer, &fullRect);

    renderParams(renderer);

    // Graph background
    SDL_SetRenderDrawColor(renderer, 18, 18, 22, 255);
    SDL_RenderFillRect(renderer, &graphRect);

    // Spectrum fill
    {
        int sr = AudioManager::instance()->sampleRate;
        if (sr <= 0) sr = 48000;
        auto& ring = showWet ? wetRing : dryRing;
        float logMin2 = std::log(freqMin), logMax2 = std::log(freqMax);
        std::vector<SDL_FPoint> specPts;
        specPts.reserve(spectrumBins + 2);
        specPts.push_back({graphRect.x, graphRect.y + graphRect.h});
        for (int b = 0; b < spectrumBins; b++) {
            float t = static_cast<float>(b) / (spectrumBins - 1);
            float fHz = std::exp(logMin2 + t * (logMax2 - logMin2));
            float re = 0, im = 0;
            for (int n = 0; n < fftSize; n++) {
                int idx = (ringWrite - 1 - n + fftSize) % fftSize;
                float s = ring[idx];
                float w = 2.0f * 3.14159265f * fHz * static_cast<float>(n) / static_cast<float>(sr);
                re += s * std::cos(w);
                im += s * std::sin(w);
            }
            float mag = std::sqrt(re * re + im * im) / static_cast<float>(fftSize);
            float magDb = 20.0f * std::log10(std::max(mag, 1e-8f));
            guiSpectrum.resize(spectrumBins);
            guiSpectrum[b] = magDb;
            float normG = (magDb - gainDbMin) / (gainDbMax - gainDbMin);
            float y = graphRect.y + graphRect.h * (1.0f - std::clamp(normG, 0.0f, 1.0f));
            float x = graphRect.x + t * graphRect.w;
            specPts.push_back({x, y});
        }
        specPts.push_back({graphRect.x + graphRect.w, graphRect.y + graphRect.h});
        // Draw filled polygon
        std::vector<SDL_Vertex> verts;
        for (auto& p : specPts)
            verts.push_back({p, {60, 120, 200, 40}, {0, 0}});
        std::vector<int> idxs;
        for (int i = 1; i < static_cast<int>(verts.size()) - 1; i++)
            idxs.insert(idxs.end(), {0, i, i + 1});
        SDL_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()),
                           idxs.data(), static_cast<int>(idxs.size()));
    }

    SDL_SetRenderDrawColor(renderer, 50, 50, 55, 255);
    SDL_RenderRect(renderer, &graphRect);

    // Wet/dry toggle
    if (fonts.mainFont) {
        const char* label = showWet ? "Wet" : "Dry";
        SDL_Surface* s = TTF_RenderText_Blended(fonts.mainFont, label, 0, SDL_Color{160, 200, 240, 255});
        if (s) {
            SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FRect tr{graphRect.x + graphRect.w - static_cast<float>(s->w) - 8.0f,
                         graphRect.y + 4.0f, static_cast<float>(s->w), static_cast<float>(s->h)};
            SDL_RenderTexture(renderer, tx, nullptr, &tr);
            SDL_DestroyTexture(tx); SDL_DestroySurface(s);
        }
    }

    // dB grid
    const float dbLevels[] = {gainDbMin, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, gainDbMax};
    for (float db : dbLevels) {
        float norm = (db - gainDbMin) / (gainDbMax - gainDbMin);
        float y = graphRect.y + graphRect.h * (1.0f - norm);
        SDL_SetRenderDrawColor(renderer, db == 0.0f ? 70 : 38, db == 0.0f ? 70 : 38, db == 0.0f ? 70 : 38, 255);
        SDL_RenderLine(renderer, graphRect.x, y, graphRect.x + graphRect.w, y);
        if (fonts.mainFont) {
            std::string label = std::to_string(static_cast<int>(db));
            SDL_Surface* s = TTF_RenderText_Blended(fonts.mainFont, label.c_str(), 0, SDL_Color{200, 200, 200, 255});
            if (s) {
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, s);
                SDL_FRect tr{graphRect.x - static_cast<float>(s->w) - 6.0f, y - static_cast<float>(s->h) * 0.5f,
                             static_cast<float>(s->w), static_cast<float>(s->h)};
                SDL_RenderTexture(renderer, tx, nullptr, &tr);
                SDL_DestroyTexture(tx); SDL_DestroySurface(s);
            }
        }
    }

    // Frequency grid
    const float freqMarkers[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    float logMin = std::log(freqMin), logMax = std::log(freqMax);
    for (float f : freqMarkers) {
        float t = (std::log(f) - logMin) / (logMax - logMin);
        float x = graphRect.x + t * graphRect.w;
        SDL_SetRenderDrawColor(renderer, 38, 38, 42, 255);
        SDL_RenderLine(renderer, x, graphRect.y, x, graphRect.y + graphRect.h);
        if (fonts.mainFont) {
            std::string label = (f >= 1000) ? std::to_string(static_cast<int>(f/1000)) + "k" : std::to_string(static_cast<int>(f));
            SDL_Surface* s = TTF_RenderText_Blended(fonts.mainFont, label.c_str(), 0, SDL_Color{200, 200, 200, 255});
            if (s) {
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, s);
                SDL_FRect tr{x - static_cast<float>(s->w) * 0.5f, graphRect.y + graphRect.h + 4.0f,
                             static_cast<float>(s->w), static_cast<float>(s->h)};
                SDL_RenderTexture(renderer, tx, nullptr, &tr);
                SDL_DestroyTexture(tx); SDL_DestroySurface(s);
            }
        }
    }

    // Response curve
    std::vector<SDL_FPoint> curve;
    computeResponseCurve(curve);
    if (curve.size() >= 2) {
        SDL_SetRenderDrawColor(renderer, 70, 200, 140, 255);
        for (size_t i = 0; i < curve.size() - 1; i++)
            SDL_RenderLine(renderer, curve[i].x, curve[i].y, curve[i+1].x, curve[i+1].y);
    }

    // Band handles
    for (int i = 0; i < static_cast<int>(bands.size()); i++) {
        auto& b = *bands[i];
        float freqHz = mapFreq(b.frequency.value);
        float gainDb = mapGainDb(b.gain.value);
        float t = (std::log(freqHz) - logMin) / (logMax - logMin);
        float hx = graphRect.x + t * graphRect.w;
        float normG = (gainDb - gainDbMin) / (gainDbMax - gainDbMin);
        float hy = graphRect.y + graphRect.h * (1.0f - std::clamp(normG, 0.0f, 1.0f));
        bool hover = (i == hoveredBand || i == draggingBand);
        if (hover) {
            SDL_SetRenderDrawColor(renderer, 255, 220, 80, 80);
            for (float a = 0; a < 6.283f; a += 0.1f)
                SDL_RenderLine(renderer, hx, hy, hx + std::cos(a) * 12.0f, hy + std::sin(a) * 12.0f);
        }
        float hr = (i == draggingBand) ? 7.0f : 5.0f;
        SDL_SetRenderDrawColor(renderer, hover ? 255 : 200, hover ? 220 : 180, hover ? 40 : 80, 255);
        for (float a = 0; a < 6.283f; a += 0.15f)
            SDL_RenderLine(renderer, hx, hy, hx + std::cos(a) * hr, hy + std::sin(a) * hr);
    }

    // Per-band remove buttons
    for (auto& band : bands) {
        auto& rr = band->removeRect;
        SDL_SetRenderDrawColor(renderer, 80, 40, 40, 200);
        SDL_RenderFillRect(renderer, &rr);
        SDL_SetRenderDrawColor(renderer, 200, 80, 80, 255);
        SDL_RenderRect(renderer, &rr);
        if (fonts.mainFont) {
            SDL_Surface* s = TTF_RenderText_Blended(fonts.mainFont, "x", 0, SDL_Color{255, 200, 200, 255});
            if (s) {
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, s);
                SDL_FRect tr{rr.x + (rr.w - static_cast<float>(s->w)) * 0.5f,
                             rr.y + (rr.h - static_cast<float>(s->h)) * 0.5f,
                             static_cast<float>(s->w), static_cast<float>(s->h)};
                SDL_RenderTexture(renderer, tx, nullptr, &tr);
                SDL_DestroyTexture(tx); SDL_DestroySurface(s);
            }
        }
    }

    // Add button
    auto drawBtn = [renderer](SDL_FRect r, const char* text, bool on) {
        SDL_SetRenderDrawColor(renderer, on ? 60 : 42, on ? 60 : 42, on ? 65 : 47, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, on ? 160 : 100, on ? 160 : 100, on ? 165 : 105, 255);
        SDL_RenderRect(renderer, &r);
        if (fonts.mainFont) {
            SDL_Surface* s = TTF_RenderText_Blended(fonts.mainFont, text, 0, SDL_Color{220, 220, 220, 255});
            if (s) {
                SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, s);
                SDL_FRect tr{r.x + (r.w - static_cast<float>(s->w)) * 0.5f,
                             r.y + (r.h - static_cast<float>(s->h)) * 0.5f,
                             static_cast<float>(s->w), static_cast<float>(s->h)};
                SDL_RenderTexture(renderer, tx, nullptr, &tr);
                SDL_DestroyTexture(tx); SDL_DestroySurface(s);
            }
        }
    };
    drawBtn(addBtnRect, "+ Add Band", true);
}

// --- Input ---

bool ParametricEQNode::handleCustomInput(SDL_Event& e) {
    float lMin = std::log(freqMin), lMax = std::log(freqMax);

    // --- mouse button down ---
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        // Add button
        if (msX >= addBtnRect.x && msX <= addBtnRect.x + addBtnRect.w &&
            msY >= addBtnRect.y && msY <= addBtnRect.y + addBtnRect.h) {
            int insertIdx = static_cast<int>(bands.size());
            addBand(insertIdx);
            if (project && project->um) {
                json state;
                auto& b = *bands[insertIdx];
                state["type"] = b.type.value; state["freq"] = b.frequency.value;
                state["gain"] = b.gain.value; state["q"] = b.q.value;
                std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
                project->um->newAction(new AddEQBandAction(project, std::move(mgrPath),
                    static_cast<int>(id), insertIdx, std::move(state)));
            }
            return true;
        }
        // Type dropdowns and per-band remove buttons
        for (int i = 0; i < static_cast<int>(bands.size()); i++) {
            auto& b = *bands[i];
            if (msX >= b.type.boxRect.x && msX <= b.type.boxRect.x + b.type.boxRect.w &&
                msY >= b.type.boxRect.y && msY <= b.type.boxRect.y + b.type.boxRect.h) {
                openTypeMenu(i);
                return true;
            }
            if (msX >= b.removeRect.x && msX <= b.removeRect.x + b.removeRect.w &&
                msY >= b.removeRect.y && msY <= b.removeRect.y + b.removeRect.h) {
                int remIdx = i;
                json state;
                state["type"] = b.type.value; state["freq"] = b.frequency.value;
                state["gain"] = b.gain.value; state["q"] = b.q.value;
                removeBand(remIdx);
                if (project && project->um) {
                    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
                    project->um->newAction(new RemoveEQBandAction(project, std::move(mgrPath),
                        static_cast<int>(id), remIdx, std::move(state)));
                }
                return true;
            }
        }
        // Wet/dry toggle (top-right corner of graph)
        if (msX >= graphRect.x + graphRect.w - 60.0f && msX <= graphRect.x + graphRect.w &&
            msY >= graphRect.y && msY <= graphRect.y + 20.0f) {
            showWet = !showWet;
            return true;
        }
        // Band handle click → start drag
        if (msX >= graphRect.x && msX <= graphRect.x + graphRect.w &&
            msY >= graphRect.y && msY <= graphRect.y + graphRect.h) {
            float cd = 20.0f;
            for (int i = 0; i < static_cast<int>(bands.size()); i++) {
                auto& b = *bands[i];
                float fHz = mapFreq(b.frequency.value);
                float gDb = mapGainDb(b.gain.value);
                float t = (std::log(fHz) - lMin) / (lMax - lMin);
                float hx = graphRect.x + t * graphRect.w;
                float ng = (gDb - gainDbMin) / (gainDbMax - gainDbMin);
                float hy = graphRect.y + graphRect.h * (1.0f - std::clamp(ng, 0.0f, 1.0f));
                float d = std::sqrt((msX - hx) * (msX - hx) + (msY - hy) * (msY - hy));
                if (d < cd) {
                    draggingBand = i;
                    dragStartFreq = b.frequency.value;
                    dragStartGain = b.gain.value;
                    return true;
                }
            }
        }
    }

    // --- mouse button up → end drag ---
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT && draggingBand >= 0) {
        int di = draggingBand;
        draggingBand = -1;
        if (di < static_cast<int>(bands.size()) && project && project->um) {
            auto& b = *bands[di];
            if (b.frequency.value != dragStartFreq) {
                int pi = di * 4 + 1;
                std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
                project->um->newAction(new SetParamValueUndoAction(project, mgrPath,
                    static_cast<int>(id), {static_cast<size_t>(pi)}, dragStartFreq, b.frequency.value, "Freq"));
            }
            if (b.gain.value != dragStartGain) {
                int pi = di * 4 + 2;
                std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
                project->um->newAction(new SetParamValueUndoAction(project, mgrPath,
                    static_cast<int>(id), {static_cast<size_t>(pi)}, dragStartGain, b.gain.value, "Gain"));
            }
        }
        return true;
    }

    // --- mouse motion ---
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        // Dragging a band handle
        if (draggingBand >= 0 && draggingBand < static_cast<int>(bands.size())) {
            auto& b = *bands[draggingBand];
            float t = (msX - graphRect.x) / graphRect.w;
            b.frequency.value = std::clamp(t, 0.0f, 1.0f);
            float ng = 1.0f - (msY - graphRect.y) / graphRect.h;
            b.gain.value = std::clamp(ng, 0.0f, 1.0f);
            return true;
        }
        // Hover detection
        hoveredBand = -1;
        if (msX >= graphRect.x && msX <= graphRect.x + graphRect.w &&
            msY >= graphRect.y && msY <= graphRect.y + graphRect.h) {
            float cd = 20.0f;
            for (int i = 0; i < static_cast<int>(bands.size()); i++) {
                auto& b = *bands[i];
                float fHz = mapFreq(b.frequency.value);
                float gDb = mapGainDb(b.gain.value);
                float t = (std::log(fHz) - lMin) / (lMax - lMin);
                float hx = graphRect.x + t * graphRect.w;
                float ng = (gDb - gainDbMin) / (gainDbMax - gainDbMin);
                float hy = graphRect.y + graphRect.h * (1.0f - std::clamp(ng, 0.0f, 1.0f));
                float d = std::sqrt((msX - hx) * (msX - hx) + (msY - hy) * (msY - hy));
                if (d < cd) { hoveredBand = i; break; }
            }
        }
        return true;
    }

    return false;
}

void ParametricEQNode::openTypeMenu(int bandIdx) {
    if (bandIdx < 0 || bandIdx >= static_cast<int>(bands.size())) return;
    auto& b = *bands[bandIdx];

    auto root = uTreeEntry();
    root->label = "Filter Type";

    const char* typeNames[] = {"Peaking", "Low Shelf", "High Shelf", "Low Pass", "High Pass", "Band Pass", "Notch"};
    for (int t = 0; t < 7; t++) {
        auto e = uTreeEntry();
        e->label = typeNames[t];
        float oldVal = b.type.value;
        e->click = [this, bandIdx, t, oldVal]() {
            if (bandIdx >= 0 && bandIdx < static_cast<int>(bands.size())) {
                float newVal = static_cast<float>(t) / 6.0f;
                bands[bandIdx]->type.value = newVal;
                bands[bandIdx]->coeffDirty = true;
                if (project && project->um) {
                    int paramIdx = bandIdx * 4;
                    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
                    project->um->newAction(new SetParamValueUndoAction(project, std::move(mgrPath),
                        static_cast<int>(id), {static_cast<size_t>(paramIdx)}, oldVal, newVal, "Set EQ Type"));
                }
            }
        };
        root->addChild(e);
    }

    auto* ctxMenu = ContextMenu::get();
    ctxMenu->activate();
    ctxMenu->dynamicTick = getTreeMenuTicker(root);
}

// --- Serialization ---

json ParametricEQNode::extraSerialize() {
    json j;
    j["bands"] = json::array();
    for (auto& band : bands) {
        json b;
        b["type"] = band->type.value;
        b["freq"] = band->frequency.value;
        b["gain"] = band->gain.value;
        b["q"] = band->q.value;
        j["bands"].push_back(b);
    }
    return j;
}

void ParametricEQNode::extraDeSerialize(const json& j) {
    bands.clear();
    if (j.contains("bands")) {
        for (auto& bj : j["bands"]) {
            addBand(static_cast<int>(bands.size()));
            auto& b = *bands.back();
            b.type.value = bj.value("type", 0.0f);
            b.frequency.value = bj.value("freq", 0.5f);
            b.gain.value = bj.value("gain", 0.5f);
            b.q.value = bj.value("q", 0.1f);
            b.coeffDirty = true;
        }
        updateBandPositions();
    }
}
