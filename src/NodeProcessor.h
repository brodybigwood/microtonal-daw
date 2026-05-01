#pragma once

#include "Node.h"
class Project;
class NodeManager;
class PatcherNode;

class NodeProcessor {
public:
    explicit NodeProcessor(Project*);
    ~NodeProcessor();

    void setNode(Node* n) { node = n; }
    Node* getNode() const { return node; }
    NodeManager* getManager() const { return manager; }
    PatcherNode* getRootPatcher() const;
    SDL_Window* getHostWindow() const;
    SDL_Renderer* getHostRenderer() const;

    json serialize() const;
    void deSerialize(const json&);

    void process(float* in, float* out, int numIn, int numOut, int bufferSize, int sampleRate);
    void render();
    void renderPresent();
    void handleWindowInput(SDL_Event& e);

private:
    Project* project = nullptr;
    NodeManager* manager = nullptr;
    Node* node = nullptr;
    class NodeEditor* editor = nullptr;
};
