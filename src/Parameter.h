#pragma once

#include <vector>
#include <SDL3/SDL.h>
#include <string>
#include "Geometry.h"
#include "Bus.h"

struct Modulator;
class Node;

struct Parameter {
    float value = 0;
    float defaultValue = 0;
    std::string label;
    bool clampOutput = true;
    Connection* mappedConnection = nullptr;
    bool lockMapping = false;

    virtual float operator[](size_t);

    std::vector<float> vx;
    std::vector<float> vy;

    virtual void render(SDL_Renderer*) {}
    virtual void handleInput(SDL_Event&) {}

    Parameter() = default;
    Parameter(float, std::pair<std::vector<float>, std::vector<float>>);
    ~Parameter();

    // textures holds pointers INTO the owner. Copying/moving one Parameter must
    // never carry that list over, or both instances destroy the same textures
    // (heap-buffer-overflow in clearTextures when SampleSlabs are reordered).
    Parameter(const Parameter&);
    Parameter& operator=(const Parameter&);
    Parameter(Parameter&&) noexcept;
    Parameter& operator=(Parameter&&) noexcept;

    std::vector<SDL_Texture**> textures;
    void clearTextures();

    // Continuous-input editing lives on the parameter itself: begin on press,
    // drag during motion, end on release. The whole drag is one undo step and
    // the initial jump from the press is included.
    void beginDrag(Node* owner, size_t paramIndex);
    void dragToValue(float v);
    void endDrag();
    bool dragging() const { return dragActive; }

private:
    Node* ownerNode = nullptr;
    size_t ownerIndex = 0;
    bool dragActive = false;
    bool dragPushed = false;
    float dragStartValue = 0.f;
    float dragLastValue = 0.f;
};

struct Modulator {
    bool centered;
    Parameter depth;
    float*& source;
    Connection* sourceConnection = nullptr;

    float operator[](size_t);

    Modulator(float*&, bool, std::pair<std::vector<float>, std::vector<float>> depthPolygon, float depthValue = 0.5, Connection* sourceConnection = nullptr);
};

struct Knob : Parameter {

    float thetaMin;
    float thetaMax;
    std::string label;         // API/serialization name
    std::string displayLabel;  // optional short text drawn on the canvas
    SDL_Color labelColor{0, 0, 0, 255};

    void handleInput(SDL_Event&) override;
    std::string filepath;
    SDL_FRect knobRect;
    SDL_Texture* texture = nullptr;
    // Cached textures belong to the renderer that created them; a re-opened
    // window has a new renderer, so the cache must be rebuilt.
    SDL_Renderer* textureRenderer = nullptr;
    // Values are not always normalized (e.g. sampler pitch is a MIDI note).
    float valueMin = 0.0f;
    float valueMax = 1.0f;
    float wheelStep = 0.0f; // 0 = 5% of the range
    void setValueRange(float lo, float hi, float step = 0.0f) {
        valueMin = lo;
        valueMax = hi;
        wheelStep = step;
    }
    float wheelVelocity = 0.0f;
    void render(SDL_Renderer*) override;
    void reposition(float newX, float newY, float newR);
    Knob(float, float, float, float, std::string, float, float, std::string label = "",
         SDL_Color labelColor = {0, 0, 0, 255});
    Knob(Knob&&) noexcept;
    Knob& operator=(Knob&&) noexcept;
};

// Vertical value bar. Owns its press/drag/release input and the undo step for
// the whole gesture (via Parameter::beginDrag/dragToValue/endDrag).
struct Bar : Parameter {
    SDL_FRect barRect;
    Node* ownerNode = nullptr;
    size_t ownerIndex = 0;

    Bar(float value, float x, float y, float w, float h);
    void bind(Node* owner, size_t index) { ownerNode = owner; ownerIndex = index; }
    void reposition(float x, float y, float w, float h);
    void render(SDL_Renderer*) override;
    void handleInputAt(SDL_Event&, float mx, float my);
};

struct DropdownParameter : Parameter {
    std::vector<std::string> choices;
    SDL_FRect boxRect;

    float operator[](size_t) override;
    void render(SDL_Renderer*) override;
    void handleInput(SDL_Event&) override;

    size_t getChoiceIndex() const;
    const std::string& getChoice() const;
    void reposition(float newX, float newY, float newW, float newH);

    DropdownParameter(float value, float x, float y, float w, float h,
                      std::vector<std::string> choices);
};
