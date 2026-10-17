#pragma once
#include "Node.h"

#define BUS_NUM_INPUTS 8

class BusNode : public Node {
public:
    BusNode(uint16_t, NodeManager*);
    void process() override;
    void setup() override;

    Connection* out = nullptr;
    void addInput();
    int maxSourceChannels() const;
};
