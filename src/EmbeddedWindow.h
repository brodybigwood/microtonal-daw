#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <functional>

class EmbeddedWindow {
public:
    EmbeddedWindow();
    virtual ~EmbeddedWindow() = default;

    float x = 100.f, y = 100.f;
    float w = 400.f, h = 300.f;
    int zOrder = 0;
    bool visible = true;
    std::string title;

    void close() { visible = false; }
    void open() { visible = true; }

    /** Hit-test in window coordinates (caller offsets to world space). */
    bool hitTest(float worldMx, float worldMy) const;

    /** Render chrome + content. Virtual so subclasses can customize the window shape. */
    virtual void render(SDL_Renderer* r);

    /** Handle input; returns true if consumed. Virtual so subclasses can customize. */
    virtual bool handleInput(SDL_Event& e);

    /** World-space rect (for broad-phase / z-order hit). */
    SDL_FRect worldRect() const;

    static constexpr float kTitleBarH = 24.f;
    static constexpr float kBorderW = 3.f;

    /** Override to handle keyboard events when this window is focused. */
    virtual bool handleKeyboard(SDL_Event&) { return false; }

protected:
    virtual void renderContent(SDL_Renderer*) {}
    virtual bool handleContentInput(SDL_Event&) { return false; }

    /** Subclass fills polygon vertices for custom hit-test shape (world coords). */
    virtual void buildHitPolygon(std::vector<SDL_FPoint>& out) const;

    /** Override to customize window body shape. */
    virtual void renderChrome(SDL_Renderer* r);

    bool dragging_ = false;
    float dragOffX_ = 0.f, dragOffY_ = 0.f;

private:
    SDL_FRect closeButtonRect() const;
};
