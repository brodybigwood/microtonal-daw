#pragma once

#include <SDL3/SDL.h>
#include "TreeEntry.h"
#include "nodes/nodetypes.h"

class Node;
class Bus;

class NodeEditor {
    public:
        NodeEditor();
        ~NodeEditor();

        static NodeEditor* get();

        void tick();
        void handleInput(SDL_Event&);

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

    private:
        SDL_Window* window;
        SDL_Renderer* renderer;

        SDL_FRect nodeRect;

        void renderInputs();

        void zoom(float);
        void move();

        int windowWidth = 800;
        int windowHeight = 600;

        float topMargin = 20.0f;
        float leftMargin = 0.0f;

        uint32_t lastLeftClick;

        void doubleClick();
        void createNode(NodeType);

        void moveMouse();
        void clickMouse(SDL_Event&);
        void keydown(SDL_Event&);

        Node* hoveredNode = nullptr;
        Bus* hoveredBus = nullptr;

        Node* dstNode = nullptr;
        int dstNodeID = -1;

        Node* srcNode = nullptr;
        int srcNodeID = -1;

        Bus* srcBus = nullptr;

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
