#pragma once

#include <SDL_ttf.h>

class Playhead;
class Project;
class fract;

class GridView {
    public:
        GridView();

        ~GridView();

        //rendering
        SDL_Renderer* renderer;
        void setRenderColor(SDL_Renderer*, uint8_t*);

        bool refreshGrid = false;
        virtual void UpdateGrid() {};

        Playhead* playHead;
        Project* project;

        double divHeight;

        SDL_FRect gridRect;

        double cellHeight;
        double cellWidth;
        double barWidth = 80;

        double notesPerBar = 4;

        float width;
        float height;
        float x = 0;
        float y = 0;

        //window
        SDL_Window* window;
        int windowWidth;
        int windowHeight;

        //mouse
        float mouseX, mouseY;

        int hoveredElement;

        int scrollX = 0;
        int scrollY = 0;

        int scrollSensitivity = 10;
        float scaleSensitivity = 1.1;

        double scaleX = 1;
        double scaleY = 1;

        bool lmb = false;
        bool rmb = false;

        float last_lmb_x, last_lmb_y;

        //keyboard
        bool isShiftPressed = false;
        bool isCtrlPressed = false;
        bool isAltPressed = false;


        void handleInput(SDL_Event&);
        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);

        virtual void handleCustomInput(SDL_Event&) {};
        virtual void clickMouse(SDL_Event&) {};

        virtual void createElement() {};
        virtual void deleteElement() {};

        fract getHoveredTime();

};
