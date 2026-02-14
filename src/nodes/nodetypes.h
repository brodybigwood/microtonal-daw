#pragma once
#include "Node.h"
#include "nodes/delay/delay.h"
#include "nodes/osc/osc.h"
#include "nodes/merger/merger.h"

enum NodeType {
    Oscillator,
    Merger,
    Delay,
    Count // for iteration
};

inline std::string NodeTypeStr[] = {
    "Oscillator",
    "Merger",
    "Delay"
};
