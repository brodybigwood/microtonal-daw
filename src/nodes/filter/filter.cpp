#include "filter.h"
#include "styles.h"
#include "ContextMenu.h"
#include <algorithm>
#include <cmath>
#include <string>

float FilterNode::Biquad::process(float x) {
    float y = x * b0 + z1;
    z1 = x * b1 + z2 - a1 * y;
    z2 = x * b2 - a2 * y;
    return y;
}

void FilterNode::Biquad::reset() {
    z1 = 0.0f;
    z2 = 0.0f;
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
    // 0.2..20
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
    const float qRaw = mapQ(resonanceNorm);
    // Resonance compensation for cascaded slopes to avoid runaway peaks.
    const float slopeFactor = std::max(1.0f, static_cast<float>(slopeDb()) / 12.0f);
    const float q = std::clamp(qRaw / std::sqrt(slopeFactor), 0.2f, 12.0f);
    const float w0 = 2.0f * std::numbers::pi_v<float> * fc / static_cast<float>(sampleRate);
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float alpha = s / (2.0f * q);

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;

    switch (mode) {
        case FilterMode::LowPass:
            b0 = (1.0f - c) * 0.5f;
            b1 = 1.0f - c;
            b2 = (1.0f - c) * 0.5f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * c;
            a2 = 1.0f - alpha;
            break;
        case FilterMode::HighPass:
            b0 = (1.0f + c) * 0.5f;
            b1 = -(1.0f + c);
            b2 = (1.0f + c) * 0.5f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * c;
            a2 = 1.0f - alpha;
            break;
        case FilterMode::BandPass:
            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            a0 = 1.0f + alpha;
            a1 = -2.0f * c;
            a2 = 1.0f - alpha;
            break;
    }

    const auto plan = slopePlan();
    const int neededStages = std::min(4, plan.fullStages + (plan.halfExtraStage ? 1 : 0));
    for (int i = 0; i < neededStages; ++i) {
        stages[i].b0 = b0 / a0;
        stages[i].b1 = b1 / a0;
        stages[i].b2 = b2 / a0;
        stages[i].a1 = a1 / a0;
        stages[i].a2 = a2 / a0;
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
            y = stages[s].process(y);
        }
        if (plan.halfExtraStage) {
            const float extra = stages[plan.fullStages].process(y);
            y = 0.5f * (y + extra);
        }
        out->buffer[i] = y;
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
    ctxMenu->active = true;
    ctxMenu->window_id = SDL_GetWindowID(window);
    ctxMenu->renderer = renderer;
    ctxMenu->locX = detached ? msX : *mouseX;
    ctxMenu->locY = detached ? msY : *mouseY;
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
    ctxMenu->active = true;
    ctxMenu->window_id = SDL_GetWindowID(window);
    ctxMenu->renderer = renderer;
    ctxMenu->locX = detached ? msX : *mouseX;
    ctxMenu->locY = detached ? msY : *mouseY;
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
