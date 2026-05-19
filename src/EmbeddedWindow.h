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
    int id = -1;

    void close() { visible = false; }
    void open() { visible = true; }
    virtual void moveTo(float nx, float ny) { x = nx; y = ny; markPolygonDirty(); }

    /** Restore full geometry (for undo). Override to sync derived state. */
    virtual void applyGeometry(float nx, float ny, float nw, float nh) {
        x = nx; y = ny; w = nw; h = nh;
        markPolygonDirty();
    }

    enum class ResizeZone : uint8_t { None, N, S, E, W, NE, NW, SE, SW };

    /** Hit-test against the window polygon. */
    bool hitTest(float worldMx, float worldMy) const;

    /** Which resize zone the point is near, or None. Virtual for custom shapes. */
    virtual ResizeZone getResizeZone(float worldMx, float worldMy) const;

    /** Render chrome + content. Virtual so subclasses can customize the window shape. */
    virtual void render(SDL_Renderer* r);

    /** Handle input; returns true if consumed. Virtual for subclasses. */
    virtual bool handleInput(SDL_Event& e);

    /** Handle resize motion / release / mousedown-start. Called by WindowHandler. */
    virtual bool handleResizeInput(SDL_Event& e, float mouseX, float mouseY, bool shiftHeld = false);

    /** World-space bounding rect. */
    SDL_FRect worldRect() const;

    static constexpr float kTitleBarH = 24.f;
    static constexpr float kBorderW = 3.f;
    static constexpr float kResizeHandleSz = 6.f;
    static constexpr float kMinW = 120.f;
    static constexpr float kMinH = 80.f;

    /** Override to handle keyboard events when this window is focused. */
    virtual bool handleKeyboard(SDL_Event&) { return false; }

protected:
    virtual void renderContent(SDL_Renderer*) {}
    virtual bool handleContentInput(SDL_Event&) { return false; }

    /** Subclass fills polygon vertices for custom hit-test shape (world coords). */
    virtual void buildHitPolygon(std::vector<SDL_FPoint>& out) const;

    /** Call when position or size changes so polygon cache is invalidated. */
    void markPolygonDirty() const { polygonDirty_ = true; }

    /** Override to customize window body shape. */
    virtual void renderChrome(SDL_Renderer* r);

    SDL_FRect closeButtonRect() const;

    /** Start a resize drag; returns true if the point is near a resize edge. */
    bool startResize(float worldMx, float worldMy);

    /** Apply a resize delta; enforces kMinW/kMinH. Call during motion. */
    virtual void applyResizeDelta(float dx, float dy);

    /** True for rectangular windows that get free per-axis resize. */
    virtual bool hasRectResize() const { return true; }

    /** Minimum size; rect subclasses can override per-axis. */
    virtual float minW() const { return kMinW; }
    virtual float minH() const { return kMinH; }

    /** Content scale set by uniform-scale top-corner resize. */
    float zoom_ = 1.0f;

    bool dragging_ = false;
    float dragOffX_ = 0.f, dragOffY_ = 0.f;

    bool resizing_ = false;
    ResizeZone resizeZone_ = ResizeZone::None;
    float resizeStartMouseX_ = 0.f, resizeStartMouseY_ = 0.f;
    float resizeStartX_ = 0.f, resizeStartY_ = 0.f, resizeStartW_ = 0.f, resizeStartH_ = 0.f;
    float resizeStartZoom_ = 1.0f;

    mutable std::vector<SDL_FPoint> cachedPolygon_;
    mutable bool polygonDirty_ = true;
};
