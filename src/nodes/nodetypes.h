#pragma once
#include "Node.h"
#include "nodes/delay/delay.h"
#include "nodes/osc/osc.h"
#include "nodes/merger/merger.h"
#include "nodes/splitter/splitter.h"
#include "nodes/panner/panner.h"
#include "nodes/filter/filter.h"
#include "nodes/envelope/envelope.h"
#include "nodes/visualizer/visualizer.h"
#include "nodes/arranger/arranger.h"
#include "nodes/patcher/patcher.h"
#include "nodes/multiplexer/multiplexer.h"
#include "nodes/gain/gain.h"
#include "nodes/parametriceq/parametriceq.h"

inline std::string NodeTypeStr[] = {
    "Arranger",
    "Oscillator",
    "Merger",
    "Splitter",
    "Delay",
    "Panner",
    "Filter",
    "Envelope",
    "Visualizer",
    "Patcher",
    "Multiplexer",
    "Gain",
    "ParametricEQ"
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
        case NodeType::Filter:
            return new FilterNode(id, nm);
        case NodeType::Envelope:
            return new EnvelopeNode(id, nm);
        case NodeType::Visualizer:
            return new VisualizerNode(id, nm);
        case NodeType::Patcher:
            return new PatcherNode(id, nm);
        case NodeType::Multiplexer:
            return new MultiplexerNode(id, nm);
        case NodeType::Gain:
            return new GainNode(id, nm);
        case NodeType::ParametricEQ:
            return new ParametricEQNode(id, nm);
        default:
            return nullptr;
    }
}
