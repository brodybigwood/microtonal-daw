#pragma once

#include "Node.h"

class MultiplexerNode;

class PatcherNode : public Node {
public:
    PatcherNode(uint16_t, NodeManager*);
    ~PatcherNode();

    void process() override;
    void setup() override {}

    NodeEditor* mainEditor = nullptr;
    NodeManager* mainManager;
    MultiplexerNode* multiplexer = nullptr;

    /// Leading waveform outputs [0..count) mirror inner OutputNode inputs (same channel index).
    void setLinkedWaveformChannelCount(size_t count);

    /// Trailing event outputs (after waveform block) mirror inner OutputNode event outputs.
    void setLinkedEventOutputCount(size_t count);

    /// Leading waveform inputs [0..count) mirror inner InputNode outputs (same channel index).
    void setLinkedWaveformInputCount(size_t count);

    /// Trailing event inputs (after waveform block) mirror inner InputNode event outputs.
    void setLinkedEventInputCount(size_t count);

    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    bool showConnectionPorts() const override { return multiplexer == nullptr; }

    SDL_Texture* neTex = nullptr;

    SDL_FRect neRect{0, 0, TEX_W, TEX_H};
    std::vector<float> inputPatchBuffer;
    std::vector<float> patchBuffer;

    void clearCustomTextures() override;

    json extraSerialize() override;
    void extraDeSerialize(json) override;

private:
    size_t leadingWaveformOutputCount() const;
    void insertLinkedWaveformAtEndOfBlock();
    void removeLastLinkedWaveformFromBlock();
    void insertWaveformOutputAt(int index);
    void removeWaveformOutputAt(int index);

    size_t trailingEventOutputCount() const;
    void appendEventOutput();
    void removeLastTrailingEventOutput();

    size_t leadingWaveformInputCount() const;
    void insertWaveformInputAt(int index);
    void removeWaveformInputAt(int index);
    size_t trailingEventInputCount() const;
    void appendEventInput();
    void removeLastTrailingEventInput();
};
