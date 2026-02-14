#pragma once
#include "Node.h"
#include "nodes/delay/delay.h"
#include "nodes/osc/osc.h"
#include "nodes/merger/merger.h"
#include "nodes/splitter/splitter.h"

enum NodeType {
    Oscillator,
    Merger,
    Splitter,
    Delay,
    Count // for iteration
};

inline std::string NodeTypeStr[] = {
    "Oscillator",
    "Merger",
    "Splitter",
    "Delay"
};
