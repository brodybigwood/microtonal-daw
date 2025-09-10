#pragma once
#include <vector>
#include <SDL_ttf.h>
#include "fract.h"

class Playhead;
class Project;
class Transport;

class GridView {
    public:
        GridView(bool*, SDL_FRect*, float leftMargin);

        ~GridView();

        float yMin = 0;

        //rendering
        SDL_Renderer* renderer;

        SDL_Texture* gridTexture;

        void setRenderColor(uint8_t*);
        bool tick();
        virtual bool customTick() {};

        Transport* transport;
        SDL_FRect* tRect;

        bool* detached;

        bool refreshGrid = false;
        virtual void UpdateGrid() {};
        void RenderGridTexture();

        std::vector<float> lines;
        std::vector<float> times;

        int yOffset = 0;
        int xOffset = 0;

        bool running = true;

        Playhead* playHead;
        Project* project;

        float divHeight;
        float maxHeight = 1000;
        float minHeight = 5;

        float leftMargin = 0;
        float rightMargin = 0;
        float topMargin = 50;
        float bottomMargin = 0;

        SDL_FRect gridRect;
        SDL_FRect* dstRect;

        float cellHeight;
        float dW = 40;

        double notesPerBar = 4;
        fract* startTime = new fract;

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


        void moveMouse();
        bool handleInput(SDL_Event&);
        void toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar);

        virtual void handleCustomInput(SDL_Event&) {};
        virtual void clickMouse(SDL_Event&) {};
        virtual float getY(float) = 0;
        float getX(float);

        virtual void createElement() {};
        virtual void deleteElement() {};

        void createGridRect();

        fract getHoveredTime();

        float lastHoveredLine;
        float lastHoveredTime;

};
