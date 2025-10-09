#pragma once

#include "Node.h"

class OutputNode : public Node {
    public:

        OutputNode();

        float* output;

        int numChannels;

        void process() override;

        void setup() override;
};
