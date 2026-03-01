#include "Node.h"

class PannerNode : public Node {
    public:
        PannerNode(uint16_t);

        void process() override;
        void setup() override;

        Connection* in = nullptr;
        Connection* l = nullptr;
        Connection* r = nullptr;

        Knob pan = Knob(0.5, 960, 540, 200, "assets/knobs/1.png");
};
