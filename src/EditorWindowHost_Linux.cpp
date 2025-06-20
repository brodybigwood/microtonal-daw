#include "EditorWindowHost.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <memory>
#include <iostream>

class LinuxEditorWindowHost : public EditorWindowHost {
public:

    Atom wmDeleteMessage;

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


        wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &wmDeleteMessage, 1);

    }

    ~LinuxEditorWindowHost() override {
        std::cout << "~LinuxEditorWindowHost destructor called" << std::endl;
        if (display && window) {
            XDestroyWindow(display, window);
            window = 0;  // Invalidate handle
        }
        if (display) {
            XCloseDisplay(display);
            display = nullptr;
        }
    }



    void resize(int w, int h) override {
        std::cout << "resizing" <<std::endl;
        XResizeWindow(display, window, w, h);
        XFlush(display);
    }

    bool setName(const char* name) override {
        std::cout << "naming" <<std::endl;
        if (!display || !window) return false;
        XStoreName(display, window, name);
        XFlush(display);  // Ensure the name change is processed
        return true;
    }


    bool tick() override {
        std::cout << "ticking" <<std::endl;
        if (!display || !windowOpen) {
            std::cerr << "Display is null or window is closed" << std::endl;
            return false;
        }


        while (XPending(display) > 0) {
            XEvent e;
            XNextEvent(display, &e);
            if (e.type == ConfigureNotify) {
                Steinberg::ViewRect newRect;
                newRect.left = 0;
                newRect.top = 0;
                newRect.right = e.xconfigure.width;
                newRect.bottom = e.xconfigure.height;
                view->onSize(&newRect);
            }

            if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == wmDeleteMessage) {
                std::cout << "closing" << std::endl;

                windowOpen = false;
                return false;
            }

        }

        return 1;
    }

    void* getNativeWindowHandle() override {
        std::cout << "getNativeWindowHandle" <<std::endl;
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
        std::cout << "getPlatformType" <<std::endl;
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
