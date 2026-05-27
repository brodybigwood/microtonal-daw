#pragma once

#include "Node.h"
#include "ContextMenu.h"
#include <vector>
#include <memory>
#include <array>

enum class EQFilterType {
    Peaking = 0,
    LowShelf,
    HighShelf,
    LowPass,
    HighPass,
    BandPass,
    Notch
};

inline const char* eqFilterTypeLabel(EQFilterType t) {
    switch (t) {
        case EQFilterType::Peaking:   return "Peaking";
        case EQFilterType::LowShelf:  return "Low Shelf";
        case EQFilterType::HighShelf: return "High Shelf";
        case EQFilterType::LowPass:   return "Low Pass";
        case EQFilterType::HighPass:  return "High Pass";
        case EQFilterType::BandPass:  return "Band Pass";
        case EQFilterType::Notch:     return "Notch";
    }
    return "?";
}

struct EQBand {
    DropdownParameter type;
    Knob frequency;
    Knob gain;
    Knob q;
    SDL_FRect removeRect;

    // Biquad state
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    bool coeffDirty = true;

    float lastFreqHz = -1, lastGainDb = 0, lastQ = 0;
    size_t lastTypeIdx = 999;

    EQBand(float typeVal, float freqX, float freqY, float freqR,
           float gainX, float gainY, float gainR,
           float qX, float qY, float qR,
           float dropX, float dropY, float dropW, float dropH);

    void computeCoefficients(float freqHz, float gainDb, float qVal, int typeIdx, int sr);
    float processSample(float x);
    void reset();
};

class ParametricEQNode : public Node {
public:
    ParametricEQNode(uint16_t, NodeManager*);
    ~ParametricEQNode() override;

    void process() override;
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    void setup() override;

    json extraSerialize() override;
    void extraDeSerialize(const json&) override;

    void addBand(int insertAt = -1);
    void removeBand(int index);

    std::vector<std::unique_ptr<EQBand>> bands;

    int hoveredBand = -1;
    int draggingBand = -1;
    float dragStartFreq = 0, dragStartGain = 0;

    // FFT spectrum
    static constexpr int fftSize = 2048;
    static constexpr int spectrumBins = 128;
    std::array<float, fftSize> dryRing{}, wetRing{};
    int ringWrite = 0;
    std::vector<float> guiSpectrum;
    bool showWet = false;
    void syncToGui();

private:
    Connection* in = nullptr;
    Connection* out = nullptr;

    SDL_FRect graphRect{40, 15, 1200, 360};
    SDL_FRect addBtnRect{40, 692, 110, 30};

    static constexpr float freqMin = 20.0f;
    static constexpr float freqMax = 20000.0f;
    static constexpr float gainDbMin = -18.0f;
    static constexpr float gainDbMax = 18.0f;
    static constexpr float qMin = 0.1f;
    static constexpr float qMax = 10.0f;

    float mapFreq(float norm) const;
    float mapGainDb(float norm) const;
    float mapQ(float norm) const;

    void updateBandPositions();
    void computeResponseCurve(std::vector<SDL_FPoint>& pts) const;
    void openTypeMenu(int bandIdx);
    void buildBandParams();
};
