#pragma once

#include "Node.h"
#include <vector>

class VisualizerNode : public Node {
public:
    VisualizerNode(uint16_t, NodeManager*);

    void process() override;
    void renderContent(SDL_Renderer*) override;
    void setup() override;

private:
    Connection* in = nullptr;
    Connection* out = nullptr;

    std::vector<float> levelHistory;
    size_t writePos = 0;
    float envLevel = 0.0f;
};
