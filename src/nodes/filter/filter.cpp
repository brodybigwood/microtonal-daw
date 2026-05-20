#include "filter.h"
#include "styles.h"
#include "ContextMenu.h"
#include <algorithm>
#include <cmath>
#include <string>

float FilterNode::SvfStage::process(float x, FilterMode mode) {
    // TPT SVF core (modulation-safe): low/band/high outputs from one state update.
    const float v1 = (g * (x - ic2eq) + ic1eq) / (1.0f + g * (g + k));
    const float v2 = ic2eq + g * v1;
    const float hp = x - k * v1 - v2;
    const float bp = v1;
    const float lp = v2;

    ic1eq = 2.0f * v1 - ic1eq;
    ic2eq = 2.0f * v2 - ic2eq;

    switch (mode) {
        case FilterMode::LowPass: return lp;
        case FilterMode::HighPass: return hp;
        case FilterMode::BandPass: return bp;
    }
    return lp;
}

void FilterNode::SvfStage::reset() {
    ic1eq = 0.0f;
    ic2eq = 0.0f;
}

FilterNode::FilterNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Filter) {
    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);

    params.push_back(&cutoff);
    params.push_back(&resonance);
}

int FilterNode::slopeDb() const {
    if (slopeOptionsDb.empty()) return 12;
    if (slopeIndex >= slopeOptionsDb.size()) return 12;
    return slopeOptionsDb[slopeIndex];
}

FilterNode::SlopePlan FilterNode::slopePlan() const {
    const int db = slopeDb();
    if (db <= 6) return {0, true};
    if (db == 18) return {1, true};
    return {std::clamp(db / 12, 1, 4), false};
}

float FilterNode::mapCutoff(float v) {
    // Log space 20Hz..20kHz
    const float lo = std::log(20.0f);
    const float hi = std::log(20000.0f);
    return std::exp(lo + std::clamp(v, 0.0f, 1.0f) * (hi - lo));
}

float FilterNode::mapQ(float v) {
    // Legacy helper kept for compatibility; SVF path uses k directly.
    return 0.2f + std::clamp(v, 0.0f, 1.0f) * 19.8f;
}

const char* FilterNode::modeLabel() const {
    switch (mode) {
        case FilterMode::LowPass: return "LP";
        case FilterMode::HighPass: return "HP";
        case FilterMode::BandPass: return "BP";
    }
    return "LP";
}

void FilterNode::updateCoefficients() {
    updateCoefficientsNormalized(cutoff.value, resonance.value);
}

void FilterNode::updateCoefficientsNormalized(float cutoffNorm, float resonanceNorm) {
    if (sampleRate <= 0) return;

    const float fc = std::clamp(mapCutoff(cutoffNorm), 20.0f, sampleRate * 0.45f);
    const float slopeFactor = std::max(1.0f, static_cast<float>(slopeDb()) / 12.0f);
    constexpr float kPi = 3.14159265358979323846f;
    const float g = std::tan(kPi * fc / static_cast<float>(sampleRate));

    // Serum-like resonance feel: gentle early range, stronger late ramp.
    float res = std::pow(std::clamp(resonanceNorm, 0.0f, 1.0f), 0.62f);
    // High slopes need extra damping to avoid over-emphasized peaks.
    res = std::clamp(res / std::sqrt(slopeFactor), 0.0f, 0.995f);
    // TPT SVF damping term.
    const float k = std::clamp(2.0f - 1.98f * res, 0.02f, 2.0f);

    // Mild output compensation so resonance+slope behaves more like synth filters.
    outputTrim = 1.0f / (1.0f + 0.22f * res * (slopeFactor - 1.0f));

    const auto plan = slopePlan();
    const int neededStages = std::min(4, plan.fullStages + (plan.halfExtraStage ? 1 : 0));
    for (int i = 0; i < neededStages; ++i) {
        stages[i].g = g;
        stages[i].k = k;
    }
    coeffDirty = false;
}

void FilterNode::process() {
    if (!out || !out->buffer) return;

    if (!in || !in->is_connected || !in->buffer) {
        std::memset(out->buffer, 0, static_cast<size_t>(bufferSize) * sizeof(float));
        return;
    }

    if (coeffDirty) {
        updateCoefficients();
        lastCutoffValue = cutoff.value;
        lastResonanceValue = resonance.value;
    }

    const auto plan = slopePlan();

    for (int i = 0; i < bufferSize; ++i) {
        const float cutoffNow = cutoff[i];
        const float resonanceNow = resonance[i];
        if (cutoffNow != lastCutoffValue || resonanceNow != lastResonanceValue) {
            updateCoefficientsNormalized(cutoffNow, resonanceNow);
            lastCutoffValue = cutoffNow;
            lastResonanceValue = resonanceNow;
        }

        float y = in->buffer[i];
        for (int s = 0; s < plan.fullStages; ++s) {
            y = stages[s].process(y, mode);
        }
        if (plan.halfExtraStage) {
            const float extra = stages[plan.fullStages].process(y, mode);
            y = 0.5f * (y + extra);
        }
        out->buffer[i] = y * outputTrim;
    }
}

void FilterNode::setup() {
    for (auto& stage : stages) stage.reset();
    lastCutoffValue = cutoff.value;
    lastResonanceValue = resonance.value;
    coeffDirty = true;
}

void FilterNode::renderContent(SDL_Renderer* renderer) {
    Node::renderContent(renderer);

    auto drawSelector = [renderer](const SDL_FRect& r, const std::string& label) {
        SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderRect(renderer, &r);
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, label.c_str(), 0, SDL_Color{0, 0, 0, 255});
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        const float scale = 1.8f;
        const float tw = static_cast<float>(surf->w) * scale;
        const float th = static_cast<float>(surf->h) * scale;
        SDL_FRect tr{
            r.x + (r.w - tw) * 0.5f,
            r.y + (r.h - th) * 0.5f,
            tw, th
        };
        SDL_RenderTexture(renderer, tex, nullptr, &tr);
        SDL_DestroyTexture(tex);
        SDL_DestroySurface(surf);
    };

    drawSelector(modeRect, std::string("Type: ") + modeLabel());
    drawSelector(slopeRect, std::string("Slope: ") + std::to_string(slopeDb()) + " dB");
}

bool FilterNode::handleCustomInput(SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT) return false;
    auto inside = [this](const SDL_FRect& r) {
        return msX >= r.x && msX <= r.x + r.w && msY >= r.y && msY <= r.y + r.h;
    };

    if (inside(modeRect)) {
        openModeMenu();
        return true;
    }
    if (inside(slopeRect)) {
        openSlopeMenu();
        return true;
    }
    return false;
}

void FilterNode::openModeMenu() {
    auto root = uTreeEntry();
    root->label = "Filter Type";

    auto addEntry = [this, &root](const std::string& label, FilterMode m) {
        auto e = uTreeEntry();
        e->label = label;
        e->click = [this, m]() {
            mode = m;
            coeffDirty = true;
        };
        root->addChild(e);
    };

    addEntry("LP", FilterMode::LowPass);
    addEntry("HP", FilterMode::HighPass);
    addEntry("BP", FilterMode::BandPass);

    auto* ctxMenu = ContextMenu::get();
    ctxMenu->activate();
    ctxMenu->dynamicTick = getTreeMenuTicker(root);
}

void FilterNode::openSlopeMenu() {
    auto root = uTreeEntry();
    root->label = "Filter Slope";

    for (size_t i = 0; i < slopeOptionsDb.size(); ++i) {
        auto e = uTreeEntry();
        e->label = std::to_string(slopeOptionsDb[i]) + " dB/oct";
        e->click = [this, i]() {
            slopeIndex = i;
            coeffDirty = true;
        };
        root->addChild(e);
    }

    auto* ctxMenu = ContextMenu::get();
    ctxMenu->activate();
    ctxMenu->dynamicTick = getTreeMenuTicker(root);
}

json FilterNode::extraSerialize() {
    json j;
    j["mode"] = static_cast<int>(mode);
    j["slopeIndex"] = slopeIndex;
    j["slopeDb"] = slopeDb();
    j["cutoff"] = cutoff.value;
    j["resonance"] = resonance.value;
    return j;
}

void FilterNode::extraDeSerialize(json j) {
    mode = static_cast<FilterMode>(j.value("mode", 0));
    slopeIndex = j.value("slopeIndex", static_cast<size_t>(0));
    if (slopeIndex >= slopeOptionsDb.size()) {
        int slopeDbLegacy = j.value("slopeDb", 12);
        auto it = std::find(slopeOptionsDb.begin(), slopeOptionsDb.end(), slopeDbLegacy);
        slopeIndex = (it == slopeOptionsDb.end()) ? 0 : static_cast<size_t>(std::distance(slopeOptionsDb.begin(), it));
    }
    cutoff.value = j.value("cutoff", cutoff.value);
    resonance.value = j.value("resonance", resonance.value);
    coeffDirty = true;
}
