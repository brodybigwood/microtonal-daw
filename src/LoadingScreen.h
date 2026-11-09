#pragma once

#include <SDL3/SDL.h>
#include <string>

/** Vista/Aero-style project loading screen. Draws and presents one frame. */
namespace loading {

void render(SDL_Renderer* r, int w, int h, double seconds, float progress,
            const std::string& stage);

/** Small glass progress badge in the same theme, for overlaying during an
    export. Draws into the current render target (no present). */
void renderExportBadge(SDL_Renderer* r, float x, float y, float w, float h,
                       float progress);

} // namespace loading
