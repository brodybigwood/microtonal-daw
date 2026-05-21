#pragma once

#include "Node.h"

class PatcherNode;

class MultiplexerNode : public Node {
public:
    MultiplexerNode(uint16_t id, NodeManager* nm);
    ~MultiplexerNode();

    std::vector<PatcherNode*> patchers;
    size_t activeIndex = 0;

    void syncPortsFromPatchers();

    void process() override;
    void setup() override;
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;

    json extraSerialize() override;
    void extraDeSerialize(json) override;
    void clearCustomTextures() override;
};
