#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <functional>

enum class SettingType { Bool, Int };

struct SettingDesc {
    SettingType type;
    const char* key;        // key in Settings
    const char* label;      // display text
    int minVal = 0;         // Int only
    int maxVal = 1000;      // Int only
    int step = 1;           // Int only
    const char* cycleLabels = nullptr; // if set on Int, render as cycle toggle (labels separated by |)
    std::function<void()> onChange;     // called after value change (e.g. to restart audio)
};

class PrefSection {
public:
    virtual ~PrefSection() = default;
    virtual const char* title() const = 0;
    virtual bool hasContent() const { return false; }

    /** Draw the section's icon as white geometry centered at (cx,cy) fitting in sz×sz. */
    virtual void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const = 0;

    virtual void renderContent(SDL_Renderer*, const SDL_FRect& /*innerBounds*/, float /*scale*/) {}

    /** Handle input within the inner-circle content area. (mx,my) in world coords. */
    virtual bool handleContentInput(SDL_Event&, float /*mx*/, float /*my*/,
                                    const SDL_FRect& /*innerBounds*/) { return false; }

    /** Setting descriptors for this section. */
    virtual const std::vector<SettingDesc>& settings() const {
        static const std::vector<SettingDesc> empty;
        return empty;
    }

    SDL_Renderer* renderer_ = nullptr;
    uint32_t window_id_ = 0;
};

class AudioSection : public PrefSection {
public:
    const char* title() const override { return "Audio"; }
    bool hasContent() const override { return true; }
    void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const override;
    void renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) override;
    bool handleContentInput(SDL_Event& e, float mx, float my,
                            const SDL_FRect& innerBounds) override;
    const std::vector<SettingDesc>& settings() const override;
};

class GUISection : public PrefSection {
public:
    const char* title() const override { return "GUI"; }
    bool hasContent() const override { return true; }
    void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const override;
    void renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) override;
    bool handleContentInput(SDL_Event& e, float mx, float my,
                            const SDL_FRect& innerBounds) override;
    const std::vector<SettingDesc>& settings() const override;
};

class ControlsSection : public PrefSection {
public:
    const char* title() const override { return "Controls"; }
    bool hasContent() const override { return true; }
    void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const override;
    void renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) override;
    bool handleContentInput(SDL_Event& e, float mx, float my,
                            const SDL_FRect& innerBounds) override;
    const std::vector<SettingDesc>& settings() const override;
};

class GeneralSection : public PrefSection {
public:
    const char* title() const override { return "General"; }
    bool hasContent() const override { return true; }
    void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const override;
    void renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) override;
    bool handleContentInput(SDL_Event& e, float mx, float my,
                            const SDL_FRect& innerBounds) override;
};
