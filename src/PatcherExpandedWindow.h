#pragma once

#include "WindowManager.h"

class PatcherNode;
class NodeEditor;
class Project;

/// A node editor (patcher or another node's inner graph) in its own top-level SDL3 window.
class PatcherExpandedWindow : public ExpandedWindow {
public:
    PatcherExpandedWindow(PatcherNode* node, Project* project);
    PatcherExpandedWindow(NodeEditor* editor, Project* project);
    ~PatcherExpandedWindow() override;

    void render() override;
    bool handleInput(SDL_Event& e) override;
    bool handleKeyboard(SDL_Event& e) override;
    void onCreated() override;

private:
    PatcherNode* patcherNode_ = nullptr;
    NodeEditor* editor_ = nullptr;
    Project* project_ = nullptr;
    bool needsInit_ = true;
    int lastW_ = 0, lastH_ = 0;
};
