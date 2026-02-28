#pragma once
#include "Node.h"
#include "nodes/delay/delay.h"
#include "nodes/osc/osc.h"
#include "nodes/merger/merger.h"
#include "nodes/splitter/splitter.h"
#include "nodes/panner/panner.h"

inline std::string NodeTypeStr[] = {
    "Oscillator",
    "Merger",
    "Splitter",
    "Delay",
    "Panner"
};
