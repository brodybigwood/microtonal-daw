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

inline Node* byType(NodeType t, int id, NodeManager* nm) {
    switch (t) {
        case NodeType::Arranger:
            return new ArrangerNode(id, nm);
        case NodeType::Oscillator:
            return new OscillatorNode(id, nm);
        case NodeType::Merger:
            return new MergerNode(id, nm);
        case NodeType::Splitter:
            return new SplitterNode(id, nm); 
        case NodeType::Delay:
            return new DelayNode(id, nm);
        case NodeType::Panner:
            return new PannerNode(id, nm);
        default:
            return nullptr;
    }
}
