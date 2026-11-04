#pragma once

#include <SDL3/SDL.h>
#include <string>

/** Vista/Aero-style project loading screen. Draws and presents one frame. */
namespace loading {

void render(SDL_Renderer* r, int w, int h, double seconds, float progress,
            const std::string& stage);

} // namespace loading
