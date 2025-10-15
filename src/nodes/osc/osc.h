#pragma oce

#include "Node.h"

class OscillatorNode : public Node {
    public:
        OscillatorNode(uint16_t);

        Connection* output;

        void process() override;
};
