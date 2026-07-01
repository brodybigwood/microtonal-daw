#include "PatcherExpandedWindow.h"
#include "nodes/patcher/patcher.h"
#include "NodeEditor.h"
#include "styles.h"

PatcherExpandedWindow::PatcherExpandedWindow(PatcherNode* node, Project* project)
    : patcherNode_(node), project_(project) {
    title = "Patcher";
}

PatcherExpandedWindow::~PatcherExpandedWindow() {}

void PatcherExpandedWindow::onCreated() {
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    lastW_ = w;
    lastH_ = h;
}

void PatcherExpandedWindow::render() {
    if (!renderer || !patcherNode_ || !patcherNode_->mainEditor) return;

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    if (w != lastW_ || h != lastH_) {
        lastW_ = w;
        lastH_ = h;
        patcherNode_->mainEditor->setEmbeddedCanvasSize(static_cast<float>(w), static_cast<float>(h));
    }

    // Update mouse coords.
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    patcherNode_->mainEditor->mouseX = mx;
    patcherNode_->mainEditor->mouseY = my;

    patcherNode_->mainEditor->tick(renderer);
}

bool PatcherExpandedWindow::handleInput(SDL_Event& e) {
    if (!patcherNode_ || !patcherNode_->mainEditor) return false;
    patcherNode_->mainEditor->handleInput(e);
    return true;
}

bool PatcherExpandedWindow::handleKeyboard(SDL_Event& e) {
    if (!patcherNode_ || !patcherNode_->mainEditor) return false;
    patcherNode_->mainEditor->handleInput(e);
    return true;
}
