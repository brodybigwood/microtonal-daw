#pragma once

#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <SDL3/SDL.h>
#include <SDL_ttf.h>

struct TreeEntry {
    std::string label;
    std::vector<std::shared_ptr<TreeEntry>> children;
    std::function<void()> click;   
    bool isParent();
    void addChild(std::shared_ptr<TreeEntry> child);

    bool isOpen = false;

    SDL_Texture* labelTexture = nullptr;
    int textWidth = 0;
    int textHeight = 0;

    ~TreeEntry();
};

std::shared_ptr<TreeEntry> uTreeEntry();
