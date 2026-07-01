#include "WindowManager.h"
#include <iostream>

// --- ExpandedWindow ---

ExpandedWindow::~ExpandedWindow() {
    onDestroy();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
}

void ExpandedWindow::show() {
    if (window) {
        SDL_ShowWindow(window);
        visible = true;
    }
}

void ExpandedWindow::hide() {
    if (window) {
        SDL_HideWindow(window);
        visible = false;
    }
}

void ExpandedWindow::setSize(int w, int h) {
    if (window) SDL_SetWindowSize(window, w, h);
}

void ExpandedWindow::setPosition(int x, int y) {
    if (window) SDL_SetWindowPosition(window, x, y);
}

void ExpandedWindow::setTitle(const char* t) {
    title = t;
    if (window) SDL_SetWindowTitle(window, t);
}

// --- WindowManager ---

WindowManager::WindowManager() {}

WindowManager::~WindowManager() {
    // Destroy all windows. ExpandedWindow dtors clean up SDL resources.
    windows_.clear();
    bySDLID_.clear();
}

ExpandedWindow* WindowManager::addWindow(std::unique_ptr<ExpandedWindow> w, int width, int height, const char* title) {
    w->window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!w->window) {
        std::cerr << "[WindowManager] Failed to create window: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    w->renderer = SDL_CreateRenderer(w->window, NULL);
    if (!w->renderer) {
        std::cerr << "[WindowManager] Failed to create renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(w->window);
        w->window = nullptr;
        return nullptr;
    }

    w->sdlWindowID = SDL_GetWindowID(w->window);
    w->title = title;
    w->id = nextID_++;

    // Auto-close callback: hide the window when WM close button is pressed.
    w->onRequestClose = [wptr = w.get()]() {
        wptr->hide();
    };

    w->onCreated();

    ExpandedWindow* raw = w.get();
    bySDLID_[w->sdlWindowID] = raw;
    windows_.push_back(std::move(w));

    return raw;
}

void WindowManager::removeWindow(ExpandedWindow* w) {
    if (!w) return;
    bySDLID_.erase(w->sdlWindowID);
    for (auto it = windows_.begin(); it != windows_.end(); ++it) {
        if (it->get() == w) {
            windows_.erase(it);
            return;
        }
    }
}

bool WindowManager::handleEvent(const SDL_Event& e) {
    // Route window events to the matching expanded window.
    if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) {
        auto it = bySDLID_.find(e.window.windowID);
        if (it == bySDLID_.end()) return false;

        ExpandedWindow* w = it->second;

        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            if (w->onRequestClose) w->onRequestClose();
            return true;
        }

        if (e.type == SDL_EVENT_WINDOW_EXPOSED || e.type == SDL_EVENT_WINDOW_RESIZED) {
            // Main render loop handles rendering. Just consume the event.
            return true;
        }

        return false;
    }

    // Route input events to the matching expanded window.
    auto it = bySDLID_.find(e.window.windowID);
    if (it == bySDLID_.end()) return false;

    ExpandedWindow* w = it->second;
    if (!w->visible) return false;

    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return w->handleKeyboard(const_cast<SDL_Event&>(e));
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_TEXT_INPUT:
            return w->handleInput(const_cast<SDL_Event&>(e));
        default:
            return false;
    }
}

void WindowManager::renderAll() {
    for (auto& w : windows_) {
        if (w->visible && w->renderer) {
            w->render();
        }
    }
}

void WindowManager::presentAll() {
    for (auto& w : windows_) {
        if (w->visible && w->renderer) {
            SDL_RenderPresent(w->renderer);
        }
    }
}

ExpandedWindow* WindowManager::findById(int id) {
    for (auto& w : windows_) {
        if (w->id == id) return w.get();
    }
    return nullptr;
}

ExpandedWindow* WindowManager::findByTitle(const char* title) {
    for (auto& w : windows_) {
        if (w->title == title) return w.get();
    }
    return nullptr;
}

void WindowManager::removeWindowsByTitleSuffix(const std::string& suffix) {
    for (auto it = windows_.begin(); it != windows_.end(); ) {
        auto& w = *it;
        if (w->title.size() >= suffix.size() &&
            w->title.compare(w->title.size() - suffix.size(), suffix.size(), suffix) == 0) {
            bySDLID_.erase(w->sdlWindowID);
            it = windows_.erase(it);
        } else {
            ++it;
        }
    }
}
