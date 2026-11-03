#include "Node.h"

#define NUM_DUPLICATE_OUTPUTS 8

class DuplicatorNode : public Node {
    public:
        DuplicatorNode(uint16_t, NodeManager*);

        void process() override;
        void setup() override;

        // Every output carries the whole input, so each follows the input's width.
        bool widthFollowsInput() const override { return true; }

        Connection* in = nullptr;

    private:
        void addOutput();
};
