#include "Node.h"

#define NUM_OUTPUTS 8

class SplitterNode : public Node {
    public:
        SplitterNode(uint16_t);

        void process() override;

        void setup() override;

        Connection* in = nullptr;

        void addOutput();
};
