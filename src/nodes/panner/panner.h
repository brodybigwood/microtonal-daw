#include "Node.h"

class PannerNode : public Node {
    public:
        PannerNode(uint16_t);

        void process() override;
        void setup() override;

        Connection* in = nullptr;
        Connection* l = nullptr;
        Connection* r = nullptr;

        Parameter pan = Parameter(0.5, {860, 440, 200, 200});
};
