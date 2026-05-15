#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#ifndef WINDOWHANDLER_H
#define WINDOWHANDLER_H
#include <vector>
#include <queue>
#include <mutex>
#include <string>
#include "Project.h"
#include "ContextMenu.h"
#include "Window.h"
#include "EmbeddedWindow.h"

class PreferencesWindow;

class WindowHandler {
    public:
    std::vector<Window*> windows;
    void addWindow(Window*);
    void removeWindow(Window*);

        WindowHandler();
        ~WindowHandler();

        SDL_Renderer* renderer;

        static WindowHandler* instance();

        bool handleKeyboard();
        bool handleMouse();

        Project* project;

        bool tick();

        Uint32 lastTime;
        bool running = true;

        double fps = 60;
    
        double frameTime = 1000/fps;   
    
        ContextMenu* ctxMenu;

        //keyboard
        bool isShiftPressed = false;
        bool isCtrlPressed = false;
        bool isAltPressed = false;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);

        void enqueueCommand(const std::string& cmd);
        void processCommands();

        // --- Pseudo-window management (embedded windows on the main canvas) ---

        /** Add a pseudo-window. Auto-assigns z-order on top. */
        EmbeddedWindow* addEmbeddedWindow(std::unique_ptr<EmbeddedWindow> w);

        /** Render all pseudo-windows on the project's main renderer. */
        void renderEmbeddedWindows();

        /** Route an event to pseudo-windows (resize check first, then window input).
         *  Returns true if a pseudo-window consumed the event. */
        bool routeEmbeddedWindowEvent(SDL_Event& e, float mouseX, float mouseY);

        /** Return existing PreferencesWindow if one is open, else nullptr. */
        PreferencesWindow* existingPreferencesWindow();

        /** Window that receives keyboard events (last clicked pseudo-window). */
        EmbeddedWindow* focusedEmbeddedWindow() const { return focusedEmbeddedWindow_; }

    private:
        std::mutex commandMutex;
        std::queue<std::string> pendingCommands;

        std::vector<std::unique_ptr<EmbeddedWindow>> embeddedWindows_;
        EmbeddedWindow* capturedEmbeddedWindow_ = nullptr;
        EmbeddedWindow* focusedEmbeddedWindow_ = nullptr;
};

#endif
