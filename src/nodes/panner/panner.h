#include "Node.h"

class PannerNode : public Node {
    public:
        PannerNode(uint16_t, NodeManager*);

        void process() override;
        void setup() override;

        Connection* in = nullptr;
        Connection* out = nullptr;

        Knob pan = Knob(0.5, NODE_W / 2, NODE_H / 2, 16, "assets/knobs/1.png", -30, 30, "Pan");
};
