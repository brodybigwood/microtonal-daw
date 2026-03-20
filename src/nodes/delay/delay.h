#pragma once

#include "Node.h"

class DelayNode : public Node {
    public:
        DelayNode(uint16_t, NodeManager*);

        void process() override;
};
