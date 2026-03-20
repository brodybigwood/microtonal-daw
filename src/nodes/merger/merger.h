#include "Node.h"

#define NUM_INPUTS 8

class MergerNode : public Node {
    public:
        MergerNode(uint16_t, NodeManager*);

        void process() override;

        void setup() override;

        Connection* out = nullptr;

        void addInput();
};
