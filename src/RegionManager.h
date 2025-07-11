#pragma once
#include <SDL3/SDL_render.h>
#include <vector>
#include <memory>
#include <SDL3/SDL.h>

class Project;

namespace DAW {
    class Region;
}

class RegionManager {
    public:
        RegionManager();
        ~RegionManager();
        Project* project;

        std::vector<std::shared_ptr<DAW::Region>>* regions;
        std::shared_ptr<DAW::Region> currentRegion;

        std::shared_ptr<DAW::Region> hoveredRegion;
        bool hoverNew = false;

        SDL_FRect* dstRect;
        SDL_Renderer* renderer;

        void render();

        float* mouseY;
        float* mouseX;

        int scrollY = 0;

        bool handleInput(SDL_Event& e);
};
