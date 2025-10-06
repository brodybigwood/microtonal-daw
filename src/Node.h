#pragma once
#include <vector>
#include <string>
#include "Bus.h"

enum inputType {
    BusE = 0,
    BusW = 1,
    NodeE = 2,
    NodeW = 3
};

struct inputNode {
    inputType type;
    uint16_t id;
    uint8_t index; //for busses, or nodes with multiple outputs
};

class Node {
    public:
        Node(uint16_t);
        ~Node();

        uint16_t id;
        std::string name;

        std::vector<inputNode> inputs;
        std::vector<Bus*> outputs;

        void* getInput(inputNode&);

        void* getOutput(uint8_t);
}; 
