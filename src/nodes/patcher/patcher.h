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
    void ensureOutputChannels(size_t count);

    void renderContent(SDL_Renderer*) override;
    void renderPresent() override;

    bool handleCustomInput(SDL_Event&) override;

    SDL_Texture* neTex = nullptr;

    SDL_FRect neRect{0, 0, TEX_W, TEX_H};
    std::vector<float> patchBuffer;

    void clearCustomTextures() override;

    json extraSerialize() override;
    void extraDeSerialize(json) override;

    void attachFinal() override;
    void detachFinal() override;
};
