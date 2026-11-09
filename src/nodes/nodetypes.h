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
#include "nodes/vst/vstnode.h"
#include "nodes/surroundtostereo/surroundtostereo.h"
#include "nodes/paramnode/paramnode.h"
#include "nodes/sampler/sampler.h"
#include "nodes/bus/bus.h"
#include "nodes/mixer/mixer.h"
#include "nodes/duplicator/duplicator.h"
#include "nodes/instrumentrack/instrumentrack.h"
#include "nodes/additive/additive.h"
#include "nodes/ducker/ducker.h"
#include "nodes/paninvert/paninvert.h"
#include "nodes/eventduplicator/eventduplicator.h"
#include "nodes/notelength/notelength.h"

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
    "VST",
    "Surround->Stereo",
    "Param",
    "Sampler",
    "Bus",
    "Mixer",
    "Duplicator",
    "MPE Rack",
    "Instrument Rack",
    "Additive",
    "Ducker",
    "Pan Invert",
    "Event Duplicator",
    "Note Length"
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
        case NodeType::Vst:
            return new VstNode(id, nm);
        case NodeType::MpeRack:
            return new VstNode(id, nm, true);
        case NodeType::SurroundToStereo:
            return new SurroundToStereoNode(id, nm);
        case NodeType::Param:
            return new ParamNode(id, nm);
        case NodeType::Sampler:
            return new SamplerNode(id, nm);
        case NodeType::Bus:
            return new BusNode(id, nm);
        case NodeType::Mixer:
            return new MixerNode(id, nm);
        case NodeType::Duplicator:
            return new DuplicatorNode(id, nm);
        case NodeType::InstrumentRack:
            return new InstrumentRackNode(id, nm);
        case NodeType::Additive:
            return new AdditiveNode(id, nm);
        case NodeType::Ducker:
            return new DuckerNode(id, nm);
        case NodeType::PanInvert:
            return new PanInvertNode(id, nm);
        case NodeType::EventDuplicator:
            return new EventDuplicatorNode(id, nm);
        case NodeType::NoteLength:
            return new NoteLengthNode(id, nm);
        default:
            return nullptr;
    }
}
