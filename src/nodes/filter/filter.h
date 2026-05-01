#pragma once

#include "Node.h"
#include <array>
#include <vector>

enum class FilterMode {
    LowPass = 0,
    HighPass = 1,
    BandPass = 2
};

class FilterNode : public Node {
public:
    FilterNode(uint16_t, NodeManager*);

    void process() override;
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    void setup() override;

    json extraSerialize() override;
    void extraDeSerialize(json) override;

private:
    struct SvfStage {
        float g = 0.0f;
        float k = 1.0f;
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;
        float process(float x, FilterMode mode);
        void reset();
    };

    struct SlopePlan {
        int fullStages = 1;
        bool halfExtraStage = false;
    };

    Connection* in = nullptr;
    Connection* out = nullptr;

    Knob cutoff = Knob(0.55f, TEX_W * 0.34f, TEX_H * 0.56f, 145.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Cutoff");
    Knob resonance = Knob(0.12f, TEX_W * 0.66f, TEX_H * 0.56f, 145.0f, "assets/knobs/1.png", -135.0f, 135.0f, "Resonance");

    SDL_FRect modeRect{180, 70, 330, 62};
    SDL_FRect slopeRect{770, 70, 330, 62};
    FilterMode mode = FilterMode::LowPass;
    std::vector<int> slopeOptionsDb{6, 12, 18, 24, 36, 48};
    size_t slopeIndex = 0;

    std::array<SvfStage, 4> stages{};
    bool coeffDirty = true;
    float lastCutoffValue = -1.0f;
    float lastResonanceValue = -1.0f;
    float outputTrim = 1.0f;

    void updateCoefficients();
    void updateCoefficientsNormalized(float cutoffNorm, float resonanceNorm);
    static float mapCutoff(float);
    static float mapQ(float);
    const char* modeLabel() const;
    int slopeDb() const;
    SlopePlan slopePlan() const;
    void openModeMenu();
    void openSlopeMenu();
};
