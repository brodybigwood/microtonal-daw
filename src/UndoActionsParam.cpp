#include "UndoManager.h"
#include "nodes/parametriceq/parametriceq.h"
#include "nodes/paramnode/paramnode.h"
#include "nodes/sampler/sampler.h"
#include "nodes/vst/vstnode.h"
#include "nodes/arranger/arranger.h"
#include "SongRoll.h"
#include "PianoRollWindow.h"
#include "GridElement.h"
#include <cmath>
#include "SDL_Events.h"
#include "styles.h"
#include <functional>
#include "Project.h"
#include "NodeProcessor.h"
#include "NodeEditor.h"
#include "nodes/nodetypes.h"
#include "NodeManager.h"
#include "InputNode.h"
#include "OutputNode.h"
#include "Note.h"
#include "PianoRoll.h"
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "UndoInternal.h"

// Parameter actions (split from UndoManager.cpp).

SetParamValueUndoAction::SetParamValueUndoAction(Project* p, std::vector<int> managerPath, int nodeID, std::vector<size_t> paramPath,
                                                 float oldValue, float newValue, std::string actionName) :
        ProjectAction(p, SetParamValue),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        paramPath(std::move(paramPath)),
        oldValue(oldValue),
        newValue(newValue) {
    skipInitialDo = true;
    name = std::move(actionName);
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                           : nm.getNode(static_cast<uint16_t>(this->nodeID));
        if (!target) throw std::runtime_error("SetParamValueUndoAction::doAction: node not found");
        if (this->paramPath.empty() || this->paramPath[0] >= target->params.size())
            throw std::runtime_error("SetParamValueUndoAction::doAction: parameter not found");
        target->params[this->paramPath[0]]->value = this->newValue;
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                           : nm.getNode(static_cast<uint16_t>(this->nodeID));
        if (!target) throw std::runtime_error("SetParamValueUndoAction::undoAction: node not found");
        if (this->paramPath.empty() || this->paramPath[0] >= target->params.size())
            throw std::runtime_error("SetParamValueUndoAction::undoAction: parameter not found");
        target->params[this->paramPath[0]]->value = this->oldValue;
    };
}

bool SetParamValueUndoAction::coalesceWith(ProjectAction* newer) {
    if (newer->type != SetParamValue) return false;
    auto* o = static_cast<SetParamValueUndoAction*>(newer);
    if (o->nodeID != nodeID || o->paramPath != paramPath) return false;
    newValue = o->newValue;
    return true;
}

ParamNodeAddModRowUndoAction::ParamNodeAddModRowUndoAction(Project* p, std::vector<int> managerPath, int nodeID) :
        ProjectAction(p, ParamNodeAddModRow),
        managerPath(std::move(managerPath)),
        nodeID(nodeID) {
    name = "Add Modulator";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<ParamNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target) throw std::runtime_error("ParamNodeAddModRow::doAction: node not found");
        target->addModulatorRow();
        target->makeConnectionRects();
        nm.markTopologyDirty();
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<ParamNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target || target->modulators.empty())
            throw std::runtime_error("ParamNodeAddModRow::undoAction: no modulator to remove");
        target->removeModulatorRow(target->modulators.size() - 1);
        target->makeConnectionRects();
        nm.markTopologyDirty();
    };
}

ParamNodeRemoveModRowUndoAction::ParamNodeRemoveModRowUndoAction(Project* p, std::vector<int> managerPath, int nodeID, size_t modIndex) :
        ProjectAction(p, ParamNodeRemoveModRow),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        modIndex(modIndex) {
    // Snapshot modulator state and connection wiring before removal.
    NodeManager& nm = requireManager(this->p, this->managerPath);
    auto* target = dynamic_cast<ParamNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
    if (target && modIndex < target->modulators.size()) {
        auto* m = target->modulators[modIndex];
        if (m) {
            savedDepth = m->depth.value;
            savedCentered = m->centered;
            if (m->sourceConnection) {
                savedConnID = m->sourceConnection->id;
                wasConnected = m->sourceConnection->is_connected;
                if (wasConnected) {
                    srcNode = static_cast<uint16_t>(m->sourceConnection->input_node);
                    srcCon = static_cast<uint16_t>(m->sourceConnection->input_connection);
                }
            }
        }
    }
    name = "Remove Modulator";
    doAction = [this]() {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<ParamNode*>(nm2.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target || this->modIndex >= target->modulators.size())
            throw std::runtime_error("ParamNodeRemoveModRow::doAction: modulator not found");
        target->removeModulatorRow(this->modIndex);
        target->makeConnectionRects();
        nm2.markTopologyDirty();
    };
    undoAction = [this]() {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<ParamNode*>(nm2.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target) return;
        auto* conn = new Connection;
        conn->type = DataType::Waveform;
        conn->dir = Direction::input;
        conn->label = "Mod " + std::to_string(this->modIndex + 1);
        target->inputs.addConnection(conn);
        // Move the connection to the correct position in the vector
        size_t insIdx = std::min(this->modIndex, target->inputs.connections.size() - 1);
        if (insIdx < target->inputs.connections.size() - 1) {
            auto it = std::find(target->inputs.connections.begin(), target->inputs.connections.end(), conn);
            if (it != target->inputs.connections.end()) {
                std::rotate(target->inputs.connections.begin() + static_cast<ptrdiff_t>(insIdx), it, it + 1);
            }
        }
        target->inputs.ids.clear();
        for (size_t i = 0; i < target->inputs.connections.size(); ++i)
            target->inputs.ids[target->inputs.connections[i]->id] = static_cast<uint16_t>(i);

        auto* mod = new Modulator(conn->buffer, this->savedCentered,
                                  generateRect(0, 0, 200, 10), this->savedDepth, conn);
        target->modulators.insert(
            target->modulators.begin() + static_cast<ptrdiff_t>(insIdx), mod);
        mod->depth.label = conn->label;
        target->params.insert(target->params.begin() + static_cast<ptrdiff_t>(insIdx), &mod->depth);
        for (size_t li = insIdx; li < target->modulators.size(); ++li) {
            target->modulators[li]->depth.label = "Mod " + std::to_string(li + 1);
            if (target->modulators[li]->sourceConnection)
                target->modulators[li]->sourceConnection->label = target->modulators[li]->depth.label;
        }
        target->makeConnectionRects();
        if (this->wasConnected && mod && mod->sourceConnection)
            nm2.makeNodeConnectionNow(this->srcNode, this->srcCon,
                                      static_cast<uint16_t>(this->nodeID),
                                      mod->sourceConnection->id);
        nm2.markTopologyDirty();
    };
}

ParamNodeToggleCenteredUndoAction::ParamNodeToggleCenteredUndoAction(Project* p, std::vector<int> managerPath, int nodeID,
                                                                     size_t modIndex, bool oldCentered, bool newCentered,
                                                                     float oldDepth, float newDepth) :
        ProjectAction(p, ParamNodeToggleCentered),
        managerPath(std::move(managerPath)),
        nodeID(nodeID),
        modIndex(modIndex),
        oldCentered(oldCentered),
        newCentered(newCentered),
        oldDepth(oldDepth),
        newDepth(newDepth) {
    skipInitialDo = true;
    name = "Toggle Centered";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<ParamNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target || this->modIndex >= target->modulators.size())
            throw std::runtime_error("ParamNodeToggleCentered::doAction: modulator not found");
        auto* mod = target->modulators[this->modIndex];
        mod->centered = this->newCentered;
        mod->depth.value = this->newDepth;
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<ParamNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target || this->modIndex >= target->modulators.size())
            throw std::runtime_error("ParamNodeToggleCentered::undoAction: modulator not found");
        auto* mod = target->modulators[this->modIndex];
        mod->centered = this->oldCentered;
        mod->depth.value = this->oldDepth;
    };
}

// ============================================================================
// Sampler undo actions
// ============================================================================

SamplerAddSampleUndoAction::SamplerAddSampleUndoAction(Project* p, std::vector<int> managerPath,
    int nodeID, std::string filepath)
    : ProjectAction(p, SamplerAddSample), managerPath(std::move(managerPath)),
      nodeID(nodeID), filepath(std::move(filepath)) {
    name = "Add Sample";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target) throw std::runtime_error("SamplerAddSample::doAction: node not found");
        target->addSample(this->filepath);
        this->insertedIndex = target->samples.size() - 1;
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target) throw std::runtime_error("SamplerAddSample::undoAction: node not found");
        target->removeSample(this->insertedIndex);
    };
}

SamplerRemoveSampleUndoAction::SamplerRemoveSampleUndoAction(Project* p, std::vector<int> managerPath,
    int nodeID, size_t sampleIndex)
    : ProjectAction(p, SamplerRemoveSample), managerPath(std::move(managerPath)),
      nodeID(nodeID), sampleIndex(sampleIndex) {
    // Snapshot from live node.
    NodeManager& nm = requireManager(p, this->managerPath);
    auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
    if (target && sampleIndex < target->samples.size()) {
        auto& s = target->samples[sampleIndex];
        filepath = s.filepath;
        vol = s.volume.value; pitch = s.pitch.value; pan = s.pan.value;
        offset = s.offset.value; cutoff = s.cutoff.value;
        attack = s.attack.value; decay = s.decay.value;
        sustain = s.sustain.value; release = s.release.value;
        solo = s.solo; mute = s.mute;
        sampleRate = s.sampleRate; numChannels = s.numChannels;
        // Snapshot connection wiring.
        size_t inIdx = 1 + sampleIndex;
        if (inIdx < target->inputs.connections.size()) {
            auto* ic = target->inputs.connections[inIdx];
            if (ic->is_connected) {
                inConnected = true;
                inSrcNode = ic->input_node;
                inSrcCon = ic->input_connection;
            }
        }
        if (sampleIndex < target->outputs.connections.size()) {
            auto* oc = target->outputs.connections[sampleIndex];
            if (oc->is_connected) {
                outConnected = true;
                outDstNode = oc->output_node;
                outDstCon = oc->output_connection;
            }
        }
    }
    name = "Remove Sample";
    doAction = [this]() {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        auto* target2 = dynamic_cast<SamplerNode*>(nm2.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target2) throw std::runtime_error("SamplerRemoveSample::doAction: node not found");
        target2->removeSample(this->sampleIndex);
    };
    undoAction = [this]() {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        auto* target2 = dynamic_cast<SamplerNode*>(nm2.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target2) throw std::runtime_error("SamplerRemoveSample::undoAction: node not found");
        target2->insertSampleAt(this->sampleIndex, this->filepath);
        if (this->sampleIndex < target2->samples.size()) {
            auto& s = target2->samples[this->sampleIndex];
            s.volume.value = this->vol;
            s.pitch.value = this->pitch;
            s.pan.value = this->pan;
            s.offset.value = this->offset;
            s.cutoff.value = this->cutoff;
            s.attack.value = this->attack;
            s.decay.value = this->decay;
            s.sustain.value = this->sustain;
            s.release.value = this->release;
            s.solo = this->solo;
            s.mute = this->mute;
            // Restore connection wiring.
            if (this->inConnected) {
                size_t inIdx = 1 + this->sampleIndex;
                if (inIdx < target2->inputs.connections.size()) {
                    auto* ic = target2->inputs.connections[inIdx];
                    nm2.makeNodeConnectionNow(this->inSrcNode, this->inSrcCon,
                                              ic->output_node, ic->output_connection);
                }
            }
            if (this->outConnected) {
                if (this->sampleIndex < target2->outputs.connections.size()) {
                    auto* oc = target2->outputs.connections[this->sampleIndex];
                    nm2.makeNodeConnectionNow(static_cast<int>(target2->id), static_cast<int>(oc->id),
                                              this->outDstNode, this->outDstCon);
                }
            }
        }
    };
}

SamplerReorderSamplesUndoAction::SamplerReorderSamplesUndoAction(Project* p, std::vector<int> managerPath,
    int nodeID, std::vector<size_t> newOrder)
    : ProjectAction(p, SamplerReorderSamples), managerPath(std::move(managerPath)),
      nodeID(nodeID), newOrder(std::move(newOrder)) {
    name = "Reorder Samples";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target) throw std::runtime_error("SamplerReorderSamples::doAction: node not found");
        target->reorderSamples(this->newOrder);
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target) throw std::runtime_error("SamplerReorderSamples::undoAction: node not found");
        // Compute inverse permutation.
        std::vector<size_t> inverse(this->newOrder.size());
        for (size_t i = 0; i < this->newOrder.size(); ++i)
            inverse[this->newOrder[i]] = i;
        target->reorderSamples(inverse);
    };
}

SamplerLoadSampleFileUndoAction::SamplerLoadSampleFileUndoAction(Project* p, std::vector<int> managerPath,
    int nodeID, size_t sampleIndex, std::string oldFilepath, std::string newFilepath,
    int oldSr, int oldNch, int newSr, int newNch)
    : ProjectAction(p, SamplerLoadSampleFile), managerPath(std::move(managerPath)),
      nodeID(nodeID), sampleIndex(sampleIndex),
      oldFilepath(std::move(oldFilepath)), newFilepath(std::move(newFilepath)),
      oldSampleRate(oldSr), oldNumChannels(oldNch),
      newSampleRate(newSr), newNumChannels(newNch) {
    name = "Load Sample File";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target) throw std::runtime_error("SamplerLoadSampleFile::doAction: node not found");
        target->setSampleFile(this->sampleIndex, this->newFilepath);
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target) throw std::runtime_error("SamplerLoadSampleFile::undoAction: node not found");
        target->setSampleFile(this->sampleIndex, this->oldFilepath);
    };
}

SamplerToggleSampleSoloUndoAction::SamplerToggleSampleSoloUndoAction(Project* p, std::vector<int> managerPath,
    int nodeID, size_t sampleIndex)
    : ProjectAction(p, SamplerToggleSampleSolo), managerPath(std::move(managerPath)),
      nodeID(nodeID), sampleIndex(sampleIndex) {
    name = "Toggle Solo";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target || this->sampleIndex >= target->samples.size()) return;
        target->samples[this->sampleIndex].solo = !target->samples[this->sampleIndex].solo;
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target || this->sampleIndex >= target->samples.size()) return;
        target->samples[this->sampleIndex].solo = !target->samples[this->sampleIndex].solo;
    };
}

SamplerToggleSampleMuteUndoAction::SamplerToggleSampleMuteUndoAction(Project* p, std::vector<int> managerPath,
    int nodeID, size_t sampleIndex)
    : ProjectAction(p, SamplerToggleSampleMute), managerPath(std::move(managerPath)),
      nodeID(nodeID), sampleIndex(sampleIndex) {
    name = "Toggle Mute";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target || this->sampleIndex >= target->samples.size()) return;
        target->samples[this->sampleIndex].mute = !target->samples[this->sampleIndex].mute;
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        auto* target = dynamic_cast<SamplerNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
        if (!target || this->sampleIndex >= target->samples.size()) return;
        target->samples[this->sampleIndex].mute = !target->samples[this->sampleIndex].mute;
    };
}

