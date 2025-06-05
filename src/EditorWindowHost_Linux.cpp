#include "EditorWindowHost.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <memory>
#include <iostream>

class LinuxEditorWindowHost : public EditorWindowHost {
public:
    LinuxEditorWindowHost() {
        display = XOpenDisplay(nullptr);
        if (!display) {
            std::cerr << "X11: Failed to open display." << std::endl;
            return;
        }

        int screen = DefaultScreen(display);
        window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                     100, 100, 600, 400, 1,
                                     BlackPixel(display, screen),
                                     WhitePixel(display, screen));

        XStoreName(display, window, "VST3 Editor Window");

        XSelectInput(display, window, ExposureMask | StructureNotifyMask);

        XMapWindow(display, window);
        XFlush(display);
        windowOpen = 1;
    }

    ~LinuxEditorWindowHost() override {
        if (display && window) {
            XDestroyWindow(display, window);
            XCloseDisplay(display);
        }
    }


    void resize(int w, int h) override {
        XResizeWindow(display, window, w, h);
        XFlush(display);
    }

    bool setName(const char* name) override {
        if (!display || !window) return false;
        XStoreName(display, window, name);
        XFlush(display);  // Ensure the name change is processed
        return true;
    }


    bool tick() override {
        if (!display) {
            std::cerr << "Display is null." << std::endl;
            return 0;
        }


        while (XPending(display) > 0) {
            XEvent e;
            XNextEvent(display, &e);
        std::cout << "ticking" <<std::endl;
        }

        return 1;
    }

    void* getNativeWindowHandle() override {
        if (window == 0) {
            std::cerr << "Window handle is null." << std::endl;
            return nullptr;
        }

        XWindowAttributes attr;
        if (XGetWindowAttributes(display, window, &attr) == 0) {
            std::cerr << "Invalid window handle or failed to get attributes." << std::endl;
            return nullptr;
        }

        std::cout << "Window Info:" << std::endl;
        std::cout << "  x: " << attr.x << std::endl;
        std::cout << "  y: " << attr.y << std::endl;
        std::cout << "  width: " << attr.width << std::endl;
        std::cout << "  height: " << attr.height << std::endl;
        std::cout << "  border_width: " << attr.border_width << std::endl;
        std::cout << "  depth: " << attr.depth << std::endl;
        std::cout << "  visual: " << attr.visual << std::endl;

        std::cout << "X11 Window ID: " << window << std::endl;


        return reinterpret_cast<void*>(window);
    }


    const char* getPlatformType() override {
        return Steinberg::kPlatformTypeX11EmbedWindowID;
        //return "x11embedwindowid";
    }

private:
    Display* display = nullptr;
    ::Window window = 0;
};

std::unique_ptr<EditorWindowHost> EditorWindowHost::create() {
    return std::make_unique<LinuxEditorWindowHost>();
}
