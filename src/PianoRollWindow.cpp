#include "PianoRollWindow.h"
#include "styles.h"

PianoRollWindow::PianoRollWindow(Region* region, Window* parent) {
    // Create the PianoRoll as an EmbeddedWindow (still uses its coordinate system internally).
    pianoRoll_ = new PianoRoll(region, parent);
    pianoRoll_->title = "Piano Roll";
}

PianoRollWindow::~PianoRollWindow() {
    // PianoRoll is deleted by the EmbeddedWindow system or manually.
    // We don't own it through unique_ptr because it's also registered as an EmbeddedWindow.
    delete pianoRoll_;
    pianoRoll_ = nullptr;
}

void PianoRollWindow::onCreated() {
    // Set initial size from the window.
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    lastW_ = w;
    lastH_ = h;

    // Sync PianoRoll's EmbeddedWindow to this SDL window.
    pianoRoll_->window = window;
    pianoRoll_->EmbeddedWindow::w = static_cast<float>(w);
    pianoRoll_->EmbeddedWindow::h = static_cast<float>(h);
    pianoRoll_->EmbeddedWindow::x = 0.f;
    pianoRoll_->EmbeddedWindow::y = 0.f;
}

void PianoRollWindow::render() {
    if (!renderer || !pianoRoll_) return;

    // Keep PianoRoll's renderer in sync with this window's renderer.
    pianoRoll_->renderer = renderer;

    // Check if window was resized.
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    if (w != lastW_ || h != lastH_) {
        lastW_ = w;
        lastH_ = h;
        pianoRoll_->EmbeddedWindow::w = static_cast<float>(w);
        pianoRoll_->EmbeddedWindow::h = static_cast<float>(h);
        needsInit_ = true;
    }

    // Initialize textures on first render or resize.
    if (needsInit_) {
        pianoRoll_->initWindow(renderer);
        needsInit_ = false;
    }

    // Sync coordinates and render.
    // pianoRollSyncCoords uses EmbeddedWindow::x, y, w, h — we set them above.
    // The function also uses SDL_GetMouseState for global mouse pos, which is fine.
    pianoRoll_->renderContent(renderer);
}

bool PianoRollWindow::handleInput(SDL_Event& e) {
    if (!pianoRoll_) return false;
    return pianoRoll_->handleContentInput(e);
}

bool PianoRollWindow::handleKeyboard(SDL_Event& e) {
    if (!pianoRoll_) return false;
    return pianoRoll_->handleKeyboard(e);
}
