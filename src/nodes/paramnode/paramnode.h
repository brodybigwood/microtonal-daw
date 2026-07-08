#pragma once

#include "Node.h"

class ParamNode : public Node {
public:
    ParamNode(uint16_t, NodeManager*);
    void process() override;
    void setup() override {}

    json extraSerialize() override;
    void extraDeSerialize(const json&) override;
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    void handleWindowInput(SDL_Event&) override;
    void clearCustomTextures() override;

    void addModulatorRow();
    void removeModulatorRow(size_t index);

    std::vector<Modulator*> modulators;

private:
    Connection* out = nullptr;

    int draggingIndex = -1;
    float oldDepth = 0.f;
    float scrollOffset = 0.f;
};
