#include "BackgroundImage.h"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace background {

namespace {
constexpr const char* kDir = "assets/backgrounds";

bool isImage(const std::filesystem::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
}
} // namespace

std::vector<std::string> listWallpapers() {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = fs::directory_iterator(kDir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file() || !isImage(it->path())) continue;
        out.push_back(it->path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string labelFor(const std::string& path) {
    std::string name = std::filesystem::path(path).stem().string();
    return name.empty() ? path : name;
}

SDL_Texture* Texture::get(SDL_Renderer* r, const std::string& p) {
    if (p.empty()) { reset(); return nullptr; }
    // Remember a failed load too, or a missing file is reopened every frame.
    if (path == p && renderer == r) return tex;
    reset();
    renderer = r;
    path = p;
    tex = IMG_LoadTexture(r, p.c_str());
    if (!tex)
        std::cerr << "[bg] cannot load " << p << ": " << SDL_GetError() << std::endl;
    return tex;
}

void Texture::reset() {
    if (tex) SDL_DestroyTexture(tex);
    tex = nullptr;
    path.clear();
    renderer = nullptr;
}

void drawCover(SDL_Renderer* r, SDL_Texture* tex, const SDL_FRect& dst) {
    if (!tex || dst.w <= 0 || dst.h <= 0) return;
    float iw = 0, ih = 0;
    if (!SDL_GetTextureSize(tex, &iw, &ih) || iw <= 0 || ih <= 0) return;
    const float scale = std::max(dst.w / iw, dst.h / ih);
    const float sw = dst.w / scale;
    const float sh = dst.h / scale;
    SDL_FRect src{(iw - sw) * 0.5f, (ih - sh) * 0.5f, sw, sh};
    SDL_RenderTexture(r, tex, &src, &dst);
}

void drawTiled(SDL_Renderer* r, SDL_Texture* tex, const SDL_FRect& dst) {
    if (!tex || dst.w <= 0 || dst.h <= 0) return;
    float iw = 0, ih = 0;
    if (!SDL_GetTextureSize(tex, &iw, &ih) || iw <= 0 || ih <= 0) return;

    // Larger than the view: cover-crop. Smaller: repeat at native size until
    // the destination is filled, clipped so no tile spills past its edges.
    if (iw >= dst.w && ih >= dst.h) {
        drawCover(r, tex, dst);
        return;
    }

    SDL_Rect oldClip{};
    const bool hadClip = SDL_RenderClipEnabled(r) && SDL_GetRenderClipRect(r, &oldClip);
    SDL_Rect clip{
        static_cast<int>(std::floor(dst.x)), static_cast<int>(std::floor(dst.y)),
        static_cast<int>(std::ceil(dst.w)), static_cast<int>(std::ceil(dst.h))};
    if (hadClip) {
        SDL_Rect inter{};
        SDL_GetRectIntersection(&oldClip, &clip, &inter);
        clip = inter;
    }
    SDL_SetRenderClipRect(r, &clip);

    for (float y = dst.y; y < dst.y + dst.h; y += ih) {
        for (float x = dst.x; x < dst.x + dst.w; x += iw) {
            SDL_FRect tile{x, y, iw, ih};
            SDL_RenderTexture(r, tex, nullptr, &tile);
        }
    }

    SDL_SetRenderClipRect(r, hadClip ? &oldClip : nullptr);
}

void drawBackground(SDL_Renderer* r, Texture& cache, const std::string& path,
                    const SDL_FRect& dst, const Uint8 rgba[4]) {
    SDL_Texture* tex = cache.get(r, path);
    if (tex) {
        drawTiled(r, tex, dst);
        return;
    }
    SDL_SetRenderDrawColor(r, rgba[0], rgba[1], rgba[2], rgba[3]);
    SDL_RenderFillRect(r, &dst);
}

} // namespace background
