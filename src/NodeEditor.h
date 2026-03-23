#pragma once

#include <SDL3/SDL.h>
#include "TreeEntry.h"
#include "nodes/nodetypes.h"
#include "Window.h"

#define SINE_SIZE 2000

class Node;

class NodeEditor : public Window {
    public:
        NodeEditor();
        ~NodeEditor();

        NodeManager* nm;

        void tick();
        void handleInput(SDL_Event&);
        void handleWindowInput(SDL_Event& e) override { handleInput(e); }

        uint32_t getWindowID();
        void renderPresent();

        float mouseX = 0;
        float mouseY = 0;
        bool leftClick = false;
    
        void setDstConn(Node*, int);
        void setSrcConn(Node*, int);
        void setMovingNode(Node*);
        void releaseMovingNode();

        bool& isAltPressed;
        bool& isCtrlPressed;

        SDL_Window* getWindow() { return window; };
        SDL_Renderer* getRenderer() { return renderer; };

        int windowWidth = 1920;
        int windowHeight = 1080;

        void renderSine(float x1, float y1, float x2, float y2, SDL_FColor);

    private:
        SDL_FRect nodeRect;

        void zoom(float);
        void move();

        float topMargin = 0.0f;
        float leftMargin = 0.0f;

        uint32_t lastLeftClick;

        void doubleClick();
        void createNode(NodeType);

        void moveMouse();
        void clickMouse(SDL_Event&);
        void keydown(SDL_Event&);

        Node* hoveredNode = nullptr;

        Node* dstNode = nullptr;
        int dstNodeID = -1;

        Node* srcNode = nullptr;
        int srcNodeID = -1;

        void makeConnection();
        void renderConnector(SDL_Renderer*);

        void hover();
        void render(SDL_Renderer*, SDL_FRect*);

        bool inside(float&, float&, SDL_FRect*);

        std::shared_ptr<TreeEntry> getClickMenu();

        Node* movingNode = nullptr;
        float moveOffX;
        float moveOffY;
};
