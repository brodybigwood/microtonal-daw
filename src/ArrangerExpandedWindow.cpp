#include "ArrangerExpandedWindow.h"
#include "nodes/arranger/arranger.h"
#include "SongRoll.h"
#include "styles.h"

ArrangerExpandedWindow::ArrangerExpandedWindow(ArrangerNode* node, Project* project)
    : arrangerNode_(node), project_(project) {
    title = "Arranger";
}

ArrangerExpandedWindow::~ArrangerExpandedWindow() {}

void ArrangerExpandedWindow::onCreated() {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    lastW_ = w;
    lastH_ = h;
}

void ArrangerExpandedWindow::render() {
    if (!renderer || !arrangerNode_) return;

    // Check resize.
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    if (w != lastW_ || h != lastH_) {
        lastW_ = w;
        lastH_ = h;
        needsInit_ = true;
    }

    // Ensure the SongRoll exists.
    arrangerNode_->ensureSongRoll();
    sl_ = arrangerNode_->sl;
    if (!sl_) return;

    // Update the ArrangerNode's slRect to match the window size.
    if (arrangerNode_->slRect) {
        *arrangerNode_->slRect = {0, 0, static_cast<float>(w), static_cast<float>(h)};
    }
    // Sync GridView dimensions.
    sl_->width = static_cast<float>(w);
    sl_->height = static_cast<float>(h);

    if (needsInit_) {
        sl_->generateTextures(renderer);
        needsInit_ = false;
    }

    // Update mouse coords for the SongRoll.
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    sl_->mouseX = mx;
    sl_->mouseY = my;

    sl_->tick(renderer);
}

bool ArrangerExpandedWindow::handleInput(SDL_Event& e) {
    if (!arrangerNode_ || !sl_) return false;
    return sl_->handleInput(e);
}

bool ArrangerExpandedWindow::handleKeyboard(SDL_Event& e) {
    return false;
}
