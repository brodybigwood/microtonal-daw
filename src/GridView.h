#pragma once
#include <vector>
#include <SDL_ttf.h>
#include "fract.h"
#include "Window.h"

class Playhead;
class Project;
class Transport;
class Home;

class GridView : public Window {
    public:
        GridView(SDL_FRect*, float leftMargin, Window*, Project*);

        virtual ~GridView();

        float yMin = 0;

        SDL_Texture* gridTexture = nullptr;

        virtual void clearTextures() {}
        virtual void generateTextures(SDL_Renderer* renderer) {}

        void setRenderColor(SDL_Renderer*, uint8_t*);
        bool tick(SDL_Renderer* renderer);
        virtual bool customTick(SDL_Renderer* renderer) { return true; }

        Transport* transport;
        SDL_FRect* tRect;

        bool refreshGrid = false;
        virtual void UpdateGrid() {};
        void RenderGridTexture(SDL_Renderer* renderer);

        std::vector<float> lines;
        std::vector<float> times;

        int yOffset = 0;
        int xOffset = 0;

        bool running = true;

        Playhead* playHead;
        Project* project;

        float divHeight = 5;
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
        bool& isShiftPressed;
        bool& isCtrlPressed;
        bool& isAltPressed;

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

        bool dropping = false;

        virtual void beginDrop(SDL_DropEvent&) {};
        virtual void dropFile(SDL_DropEvent&) {};
        virtual void renderDrop(SDL_Renderer*) {};
};
