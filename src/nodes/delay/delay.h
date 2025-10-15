#pragma once

#include "Node.h"

class DelayNode : public Node {
    public:
        DelayNode(uint16_t);

        void process() override;
};
