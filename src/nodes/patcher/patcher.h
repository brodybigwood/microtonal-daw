#pragma once

#include "Node.h"

class PatcherNode : public Node {
public:
    PatcherNode(uint16_t, NodeManager*);
    ~PatcherNode();

    void process() override;
    void setup() override {}

    NodeEditor* mainEditor = nullptr;
    NodeManager* mainManager;

    void renderContent(SDL_Renderer*) override;
    void renderPresent() override;

    bool handleCustomInput(SDL_Event&) override;

    SDL_Texture* neTex = nullptr;

    SDL_FRect neRect{0, 0, TEX_W, TEX_H};

    void clearCustomTextures() override;

    void attachFinal() override;
    void detachFinal() override;
};
