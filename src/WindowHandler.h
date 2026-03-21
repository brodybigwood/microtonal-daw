#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#ifndef WINDOWHANDLER_H
#define WINDOWHANDLER_H
#include <vector>
#include "Project.h"
#include "ContextMenu.h"
#include "Window.h"

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

        double fps = 60;
    
        double frameTime = 1000/fps;   
    
        ContextMenu* ctxMenu;

        //keyboard
        bool isShiftPressed = false;
        bool isCtrlPressed = false;
        bool isAltPressed = false;

        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);
};

#endif
