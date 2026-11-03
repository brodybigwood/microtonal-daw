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

    // Update mouse coords in this window's space. SDL_GetMouseState can report the
    // focused window's coordinates (e.g. the context-menu window when adding a node),
    // which puts new nodes at bogus positions; use the global position instead.
    float gx = 0.f, gy = 0.f;
    SDL_GetGlobalMouseState(&gx, &gy);
    int wx = 0, wy = 0;
    if (window) SDL_GetWindowPosition(window, &wx, &wy);
    editor_->mouseX = gx - static_cast<float>(wx);
    editor_->mouseY = gy - static_cast<float>(wy);

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
