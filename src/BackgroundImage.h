#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>

/** Wallpaper backgrounds for node canvases (patcher) and grid views.
    Images live in assets/backgrounds; the preferences offer every file there. */
namespace background {

/** Full paths of every image in assets/backgrounds, sorted. */
std::vector<std::string> listWallpapers();

/** Human label: file stem. */
std::string labelFor(const std::string& path);

/** One lazily loaded texture, cached per owning view. */
struct Texture {
    SDL_Texture* tex = nullptr;
    std::string path;
    SDL_Renderer* renderer = nullptr;

    /** Texture for `p` on `r`, loading it if needed; nullptr when empty or bad. */
    SDL_Texture* get(SDL_Renderer* r, const std::string& p);
    void reset();
    ~Texture() { reset(); }
};

/** Draw `tex` scaled to cover `dst`, centre-cropped. */
void drawCover(SDL_Renderer* r, SDL_Texture* tex, const SDL_FRect& dst);

/** Image when `path` is usable, otherwise a plain `rgba` fill. */
void drawBackground(SDL_Renderer* r, Texture& cache, const std::string& path,
                    const SDL_FRect& dst, const Uint8 rgba[4]);

} // namespace background
