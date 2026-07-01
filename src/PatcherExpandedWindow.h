#pragma once

#include "WindowManager.h"

class PatcherNode;
class Project;

/// PatcherNode content in its own top-level SDL3 window.
class PatcherExpandedWindow : public ExpandedWindow {
public:
    PatcherExpandedWindow(PatcherNode* node, Project* project);
    ~PatcherExpandedWindow() override;

    void render() override;
    bool handleInput(SDL_Event& e) override;
    bool handleKeyboard(SDL_Event& e) override;
    void onCreated() override;

private:
    PatcherNode* patcherNode_ = nullptr;
    Project* project_ = nullptr;
    bool needsInit_ = true;
    int lastW_ = 0, lastH_ = 0;
};
