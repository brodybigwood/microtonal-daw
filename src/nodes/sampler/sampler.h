#pragma once

#include "Node.h"
#include "Parameter.h"
#include "WindowManager.h"
#include "idManager.h"
#include <string>
#include <vector>

#ifndef __EMSCRIPTEN__
#include <sndfile.h>
#endif

struct SampleSlab {
    int id = -1;  // stable across reorder, used for param labels
    std::string filepath;
    float* buffer = nullptr;        // planar: ch * num_samples
    size_t num_samples = 0;
    int sampleRate = 0;
    int numChannels = 1;

    // Continuous controls — Knobs, auto-undo via SetParamValueUndoAction.
    // Values and positions set in SamplerNode::addSample().
    Knob volume;          // 0–1
    Knob pitch;           // MIDI note for native playback rate
    Knob pan;             // 0=L, 1=R
    Knob offset;          // sample start offset (0–1 fraction of num_samples)
    Knob cutoff;          // where playback stops (0–1 fraction of num_samples)
    Knob attack;          // ADSR (seconds, 0–5)
    Knob decay;
    Knob sustain;         // 0–1
    Knob release;

    // Discrete toggles — NOT Parameters (dedicated undo actions).
    bool solo = false;
    bool mute = false;

    SDL_Texture* thumbnail = nullptr;

    SampleSlab();
    SampleSlab(SampleSlab&& other) noexcept;
    SampleSlab& operator=(SampleSlab&& other) noexcept;
    ~SampleSlab();
    void loadFile(const std::string& path);
};

struct SamplerVoice {
    bool active = false;
    int noteId = -1;
    int sampleIndex = -1;
    float phase = 0.f;
    float pitchRatio = 1.0f;
    float pan = 0.5f;
    float envPhase = 0.f;     // seconds into envelope
    float envLevel = 0.f;     // current envelope amplitude
    int wait_on = 0;
    int wait_off = -1;

    void reset() { active = false; noteId = -1; wait_off = -1; }
};

class SamplerNode;

class SampleEditorWindow : public ExpandedWindow {
public:
    SampleEditorWindow(SamplerNode* node, size_t sampleIndex);
    void render() override;
    bool handleInput(SDL_Event& e) override;
    void setSample(size_t idx);

    SamplerNode* samplerNode;
    size_t sampleIndex = 0;

private:
    bool draggingVolume_ = false;
    float volDragStart_ = 0.f;
};

class SamplerNode : public Node {
public:
    SamplerNode(uint16_t, NodeManager*);
    ~SamplerNode() override;

    void process() override;
    void setup() override;
    void render(SDL_Renderer* r) override;
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    void handleWindowInput(SDL_Event&) override;

    json extraSerialize() override;
    void extraDeSerialize(const json&) override;

    // Sample management (non-undo "Now" methods called by undo actions).
    void addSample(const std::string& filepath = "");
    void insertSampleAt(size_t index, const std::string& filepath = "");
    void removeSample(size_t index);
    void reorderSamples(const std::vector<size_t>& newOrder);
    void setSampleFile(size_t index, const std::string& filepath);
    void rebuildConnections();

    std::vector<SampleSlab> samples;
    static constexpr int kMaxVoices = 64;
    SamplerVoice voices[kMaxVoices];

    static constexpr float kColW = 80.f;
    static constexpr float kColPad = 4.f;
    static constexpr float kThumbH = 30.f;
    static constexpr float kRowH = 14.f;
    static constexpr float kBtnSz = 12.f;
    static constexpr float kSliderH = 40.f;
    static constexpr float kFooterH = 24.f;
    static constexpr float kListTop = 4.f;
    static constexpr float kNODE_H = 180.f;

private:
    Connection* masterEventIn = nullptr;

    // List UI
    float scrollOffset_ = 0.f;
    int hoveredSampleIndex_ = -1;
    int draggingSample_ = -1;
    int dragTargetInsert_ = -1;
    float dragStartX_ = 0.f;
    float dragStartY_ = 0.f;

    // Per-sample editor window (detached SDL window)
    SampleEditorWindow* editorWindow_ = nullptr;
    void openSampleEditor(size_t index);
    void closeSampleEditor();

    // Volume slider drag
    int draggingVolumeIdx_ = -1;
    float oldVolume_ = 0.f;

    idManager sampleIdPool_;

    // Helpers
    void ensureThumbnail(size_t index, SDL_Renderer*);
    void openSampleFileDialog();
};
