#pragma once

#include <SDL3/SDL.h>
#include <string>

class PrefSection {
public:
    virtual ~PrefSection() = default;
    virtual const char* title() const = 0;
    virtual bool hasContent() const { return false; }

    /** Draw the section's icon as white geometry centered at (cx,cy) fitting in sz×sz. */
    virtual void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const = 0;

    virtual void renderContent(SDL_Renderer*, const SDL_FRect& /*innerBounds*/, float /*scale*/) {}
};

class AudioSection : public PrefSection {
public:
    const char* title() const override { return "Audio"; }
    bool hasContent() const override { return true; }
    void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const override;
    void renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) override;
};

class GUISection : public PrefSection {
public:
    const char* title() const override { return "GUI"; }
    bool hasContent() const override { return true; }
    void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const override;
    void renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) override;
};

class ControlsSection : public PrefSection {
public:
    const char* title() const override { return "Controls"; }
    bool hasContent() const override { return true; }
    void drawSymbol(SDL_Renderer* r, float cx, float cy, float sz) const override;
    void renderContent(SDL_Renderer* r, const SDL_FRect& b, float s) override;
};
