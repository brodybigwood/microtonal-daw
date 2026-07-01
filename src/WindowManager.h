#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <functional>

/// Base class for a window that gets its own top-level SDL3 window.
class ExpandedWindow {
public:
    virtual ~ExpandedWindow();

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Uint32 sdlWindowID = 0;
    int id = -1;
    std::string title;
    bool visible = true;

    virtual void render() = 0;
    virtual bool handleInput(SDL_Event& e) = 0;
    virtual bool handleKeyboard(SDL_Event& e) { return false; }

    void show();
    void hide();
    void setSize(int w, int h);
    void setPosition(int x, int y);
    void setTitle(const char* t);

    // Called when the window is first created, after SDL resources are ready.
    virtual void onCreated() {}

    // Called when the window is about to be destroyed.
    virtual void onDestroy() {}

    // Callback set by WindowManager for self-destruction request.
    std::function<void()> onRequestClose;
};

/// Manages top-level SDL3 windows alongside the main node graph window.
class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    /// Create a new top-level SDL3 window. Returns raw pointer (WindowManager owns).
    ExpandedWindow* addWindow(std::unique_ptr<ExpandedWindow> w, int width, int height, const char* title);

    /// Destroy a window by pointer.
    void removeWindow(ExpandedWindow* w);

    /// Route an SDL event to the matching expanded window (by windowID).
    /// Returns true if the event was consumed.
    bool handleEvent(const SDL_Event& e);

    /// Render all visible expanded windows (no present).
    void renderAll();

    /// Present all visible expanded windows.
    void presentAll();

    /// Find a window by logical ID.
    ExpandedWindow* findById(int id);

    /// Find a window by title. Returns nullptr if not found.
    ExpandedWindow* findByTitle(const char* title);

    /// Number of active windows.
    size_t windowCount() const { return windows_.size(); }

    /// Remove all windows whose title ends with the given suffix.
    void removeWindowsByTitleSuffix(const std::string& suffix);

private:
    std::vector<std::unique_ptr<ExpandedWindow>> windows_;
    std::unordered_map<Uint32, ExpandedWindow*> bySDLID_;
    int nextID_ = 100;
};
