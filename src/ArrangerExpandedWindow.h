#pragma once

#include "WindowManager.h"

class SongRoll;
class ArrangerNode;
class Project;

/// ArrangerNode content in its own top-level SDL3 window.
class ArrangerExpandedWindow : public ExpandedWindow {
public:
    ArrangerExpandedWindow(ArrangerNode* node, Project* project);
    ~ArrangerExpandedWindow() override;

    void render() override;
    bool handleInput(SDL_Event& e) override;
    bool handleKeyboard(SDL_Event& e) override;
    void onCreated() override;

private:
    ArrangerNode* arrangerNode_ = nullptr;
    Project* project_ = nullptr;
    SongRoll* sl_ = nullptr;
    bool needsInit_ = true;
    int lastW_ = 0, lastH_ = 0;
};
