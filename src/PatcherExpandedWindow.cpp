#include "PatcherExpandedWindow.h"
#include "nodes/patcher/patcher.h"
#include "NodeEditor.h"
#include "styles.h"

PatcherExpandedWindow::PatcherExpandedWindow(PatcherNode* node, Project* project)
    : patcherNode_(node), editor_(node ? node->mainEditor : nullptr), project_(project) {
    title = "Patcher";
}

PatcherExpandedWindow::PatcherExpandedWindow(NodeEditor* editor, Project* project)
    : editor_(editor), project_(project) {
    title = "Node Graph";
}

PatcherExpandedWindow::~PatcherExpandedWindow() {}

void PatcherExpandedWindow::onCreated() {
    // Leave lastW_/lastH_ at 0 so the first render applies the window size to the canvas;
    // otherwise the editor keeps its small embedded canvas and only draws the top-left.
    lastW_ = 0;
    lastH_ = 0;
}

void PatcherExpandedWindow::render() {
    if (!renderer || !editor_) return;

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    if (w != lastW_ || h != lastH_) {
        lastW_ = w;
        lastH_ = h;
        editor_->setEmbeddedCanvasSize(static_cast<float>(w), static_cast<float>(h));
    }

    // Update mouse coords.
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    editor_->mouseX = mx;
    editor_->mouseY = my;

    editor_->tick(renderer);
}

bool PatcherExpandedWindow::handleInput(SDL_Event& e) {
    if (!editor_) return false;
    editor_->handleInput(e);
    return true;
}

bool PatcherExpandedWindow::handleKeyboard(SDL_Event& e) {
    if (!editor_) return false;
    editor_->handleInput(e);
    return true;
}
