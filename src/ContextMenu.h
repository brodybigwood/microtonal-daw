#pragma once

#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <functional>
#include <string>
#include "TreeEntry.h"

class ContextMenu {

    public:

        float locX;
        float locY;

        bool active = false;

        static ContextMenu* get();

        void activate();
        void activate(SDL_Renderer* r, uint32_t wid);
        /** Close the menu and drop any pending ticker (window being destroyed). */
        void cancel();

        SDL_Renderer* renderer = nullptr;

        uint32_t window_id = 0;
    
        void tick(SDL_Event& e);

        std::function<bool(SDL_Event& e)> dynamicTick;
        bool keepOpenOnNextTreeLeafClick = false;
        bool skipNextEvent = false;
};

std::function<bool(SDL_Event& e)> getTextInputTicker(std::function<void(std::string text)> enter,
                                                    std::function<void()> onDismiss = {},
                                                    const std::string& initialText = "");

/** Same text box with a clickable button to its right; the button receives the
    current text and dismisses the prompt. */
std::function<bool(SDL_Event& e)> getTextInputTickerButton(std::function<void(std::string text)> enter,
                                                          std::function<void(std::string text)> onButton,
                                                          std::function<void()> onDismiss,
                                                          const std::string& buttonLabel,
                                                          const std::string& initialText = "");

std::function<bool(SDL_Event& e)> getTreeMenuTicker(std::shared_ptr<TreeEntry> t);
