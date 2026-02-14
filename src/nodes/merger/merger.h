#include "Node.h"

#define NUM_INPUTS 8

class MergerNode : public Node {
    public:
        MergerNode(uint16_t);

        void process() override;

        void setup() override;

        Connection* out = nullptr;

        void addInput();
};
