#include "X11Capture.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstring>

namespace {

struct MemIO { std::vector<uint8_t> data; Sint64 pos = 0; };

Sint64 SDLCALL memSize(void* u) { return static_cast<Sint64>(static_cast<MemIO*>(u)->data.size()); }
Sint64 SDLCALL memSeek(void* u, Sint64 off, SDL_IOWhence whence) {
    auto* m = static_cast<MemIO*>(u);
    const Sint64 end = static_cast<Sint64>(m->data.size());
    const Sint64 base = whence == SDL_IO_SEEK_SET ? 0 : (whence == SDL_IO_SEEK_CUR ? m->pos : end);
    const Sint64 np = base + off;
    if (np < 0) return -1;
    m->pos = np;
    return np;
}
size_t SDLCALL memRead(void*, void*, size_t, SDL_IOStatus*) { return 0; }
size_t SDLCALL memWrite(void* u, const void* ptr, size_t size, SDL_IOStatus*) {
    auto* m = static_cast<MemIO*>(u);
    if (m->pos + static_cast<Sint64>(size) > static_cast<Sint64>(m->data.size()))
        m->data.resize(static_cast<size_t>(m->pos + static_cast<Sint64>(size)));
    std::memcpy(m->data.data() + m->pos, ptr, size);
    m->pos += static_cast<Sint64>(size);
    return size;
}
bool SDLCALL memFlush(void*, SDL_IOStatus*) { return true; }
bool SDLCALL memClose(void*) { return true; }

bool surfaceToPng(SDL_Surface* surf, std::vector<uint8_t>& png, int& w, int& h) {
    if (!surf) return false;
    w = surf->w;
    h = surf->h;
    MemIO mem;
    SDL_IOStreamInterface iface{};
    SDL_INIT_INTERFACE(&iface);
    iface.size = memSize;
    iface.seek = memSeek;
    iface.read = memRead;
    iface.write = memWrite;
    iface.flush = memFlush;
    iface.close = memClose;
    SDL_IOStream* io = SDL_OpenIO(&iface, &mem);
    const bool saved = io && IMG_SavePNG_IO(surf, io, true);
    if (!saved) return false;
    png = std::move(mem.data);
    return !png.empty();
}

} // namespace

bool x11WindowToPng(void* handle, std::vector<uint8_t>& png, int& w, int& h) {
    if (!handle) return false;
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;
    const ::Window win = reinterpret_cast<::Window>(handle);
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy, win, &attr) || attr.width <= 0 || attr.height <= 0) {
        XCloseDisplay(dpy);
        return false;
    }
    // The plugin renders into child X11 drawables. Reading the host window
    // itself returns black; read the same screen region from the root window so
    // child drawables are included exactly as displayed.
    XRaiseWindow(dpy, win);
    XSync(dpy, False);
    ::Window root = DefaultRootWindow(dpy);
    int rx = 0, ry = 0;
    ::Window child = 0;
    if (!XTranslateCoordinates(dpy, win, root, 0, 0, &rx, &ry, &child)) {
        rx = attr.x;
        ry = attr.y;
    }
    XImage* img = XGetImage(dpy, root, rx, ry, attr.width, attr.height, AllPlanes, ZPixmap);
    if (!img) {
        XCloseDisplay(dpy);
        return false;
    }
    SDL_Surface* surf = SDL_CreateSurface(attr.width, attr.height, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) {
        XDestroyImage(img);
        XCloseDisplay(dpy);
        return false;
    }
    const unsigned rShift = img->red_mask ? static_cast<unsigned>(__builtin_ctzl(img->red_mask)) : 0;
    const unsigned gShift = img->green_mask ? static_cast<unsigned>(__builtin_ctzl(img->green_mask)) : 0;
    const unsigned bShift = img->blue_mask ? static_cast<unsigned>(__builtin_ctzl(img->blue_mask)) : 0;
    const unsigned long rMax = img->red_mask ? (img->red_mask >> rShift) : 255;
    const unsigned long gMax = img->green_mask ? (img->green_mask >> gShift) : 255;
    const unsigned long bMax = img->blue_mask ? (img->blue_mask >> bShift) : 255;

    SDL_LockSurface(surf);
    auto* pixels = static_cast<Uint32*>(surf->pixels);
    const int stride = surf->pitch / static_cast<int>(sizeof(Uint32));
    for (int y = 0; y < attr.height; ++y) {
        for (int x = 0; x < attr.width; ++x) {
            const unsigned long px = XGetPixel(img, x, y);
            const Uint8 r = static_cast<Uint8>(((px & img->red_mask) >> rShift) * 255 / rMax);
            const Uint8 g = static_cast<Uint8>(((px & img->green_mask) >> gShift) * 255 / gMax);
            const Uint8 b = static_cast<Uint8>(((px & img->blue_mask) >> bShift) * 255 / bMax);
            pixels[y * stride + x] = 0xFF000000u | (static_cast<Uint32>(r) << 16) |
                                     (static_cast<Uint32>(g) << 8) | b;
        }
    }
    SDL_UnlockSurface(surf);
    XDestroyImage(img);
    XCloseDisplay(dpy);

    const bool ok = surfaceToPng(surf, png, w, h);
    SDL_DestroySurface(surf);
    return ok;
}
