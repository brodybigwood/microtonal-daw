#pragma once
#include "Node.h"
#include "nodes/delay/delay.h"
#include "nodes/osc/osc.h"
#include "nodes/merger/merger.h"
#include "nodes/splitter/splitter.h"
#include "nodes/panner/panner.h"
#include "nodes/arranger/arranger.h"

inline std::string NodeTypeStr[] = {
    "Arranger",
    "Oscillator",
    "Merger",
    "Splitter",
    "Delay",
    "Panner"
};
