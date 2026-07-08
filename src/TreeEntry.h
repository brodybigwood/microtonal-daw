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

    /** If set, this entry renders custom content when open (instead of child list).
     *  Receives event, x, y, renderer, and a shared_ptr to this entry (so the ticker
     *  can add children or navigate). Returns true to stay open. */
    std::function<bool(SDL_Event&, float, float, SDL_Renderer*, std::shared_ptr<TreeEntry>)> customTick;
    /** Width of custom content, used to offset child tree levels. */
    float customWidth = 0;
    /** Height of custom content, set by the customTick during render. Used for hit-testing. */
    float customHeight = 0;

    /** If > 0, children list is clipped to this height and scrollable. */
    float maxListHeight = 0;
    float scrollOffset = 0;

    ~TreeEntry();
};

std::shared_ptr<TreeEntry> uTreeEntry();
