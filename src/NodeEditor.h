#pragma once

#include <SDL3/SDL.h>

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

    private:
        SDL_Window* window;
        SDL_Renderer* renderer;

        SDL_FRect nodeRect;

        void renderInputs();

        int windowWidth = 800;
        int windowHeight = 600;

        float topMargin = 20.0f;
        float leftMargin = 0.0f;

        float mouseX = 0;
        float mouseY = 0;

        uint32_t lastLeftClick;

        void doubleClick();
        void createNode();

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

        void hover();
        void render(SDL_Renderer*, SDL_FRect*);

        bool inside(float&, float&, SDL_FRect*);
};
