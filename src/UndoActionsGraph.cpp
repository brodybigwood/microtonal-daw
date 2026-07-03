#include "UndoManager.h"
#include "nodes/parametriceq/parametriceq.h"
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

// Node-graph actions: add/remove nodes, connections, IO ports (split from UndoManager.cpp).

AddNodeAction::AddNodeAction(Project* p, std::vector<int> managerPath, int type, float x, float y) :
        ProjectAction(p, AddNode),
        managerPath(std::move(managerPath)),
        nodeType(type),
        x(x),
        y(y) {
    name ="Add Node";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (hasRedoRestore) {
            if (!patcherData.is_null()) {
                for (auto& pd : patcherData) {
                    auto* p = nm.addNodeNow(pd["node"]);
                    if (p) {
                        for (auto c : pd["conns"])
                            nm.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
                    }
                }
            }
            json snap = redoNodeSnapshot;
            if (nm.ne) {
                snap["x"] = snap["x"].get<float>() + nm.ne->panOffsetX_ - this->panOffX;
                snap["y"] = snap["y"].get<float>() + nm.ne->panOffsetY_ - this->panOffY;
            }
            auto* restored = nm.addNodeNow(snap);
            if (!restored)
                throw std::runtime_error("AddNodeAction::doAction: addNodeNow(json) failed");
            nodeID = restored->id;
            for (auto c : redoConnectionsSnapshot)
                nm.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
            return;
        }
        float ax = this->x, ay = this->y;
        if (nm.ne) {
            ax += nm.ne->panOffsetX_ - this->panOffX;
            ay += nm.ne->panOffsetY_ - this->panOffY;
        }
        auto* node = nm.addNodeNow(static_cast<NodeType>(nodeType), ax, ay, nodeID);
        if (!node)
            throw std::runtime_error("AddNodeAction::doAction: addNodeNow failed");
        nodeID = node->id;
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (nodeID < 0)
            throw std::runtime_error("AddNodeAction::undoAction: invalid nodeID");
        if (!nm.snapshotNode(static_cast<uint16_t>(nodeID), redoNodeSnapshot, redoConnectionsSnapshot))
            throw std::runtime_error("AddNodeAction::undoAction: snapshotNode failed");
        hasRedoRestore = true;
        auto* mux = dynamic_cast<MultiplexerNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
        if (mux) {
            patcherData = json::array();
            std::vector<uint16_t> pids;
            for (auto* p : mux->patchers) {
                json pd;
                (void)nm.snapshotNode(p->id, pd["node"], pd["conns"]);
                patcherData.push_back(pd);
                pids.push_back(p->id);
            }
            mux->patchers.clear();
            for (auto pid : pids)
                nm.removeNodeNow(pid);
        }
        nm.removeNodeNow(nodeID);
    };
}

RemoveNodeAction::RemoveNodeAction(Project* p, std::vector<int> managerPath, int nodeID) :
        ProjectAction(p, RemoveNode),
        managerPath(std::move(managerPath)),
        nodeID(nodeID) {
    name ="Remove Node";
    doAction = [this] () {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        if (nodeData.is_null()) {
            (void)nm2.snapshotNode(static_cast<uint16_t>(this->nodeID), nodeData, connectionsData);
            auto* mux = dynamic_cast<MultiplexerNode*>(nm2.getNode(static_cast<uint16_t>(this->nodeID)));
            if (mux) {
                patcherData = json::array();
                std::vector<uint16_t> pids;
                for (auto* p : mux->patchers) {
                    json pd;
                    (void)nm2.snapshotNode(p->id, pd["node"], pd["conns"]);
                    patcherData.push_back(pd);
                    pids.push_back(p->id);
                }
                mux->patchers.clear();
                for (auto pid : pids)
                    nm2.removeNodeNow(pid);
            }
        }
        nm2.removeNodeNow(this->nodeID);
    };
    undoAction = [this] () {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        if (!patcherData.is_null()) {
            for (auto& pd : patcherData) {
                auto* p = nm2.addNodeNow(pd["node"]);
                if (p) {
                    for (auto c : pd["conns"])
                        nm2.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
                }
            }
        }
        json nd = nodeData;
        if (nm2.ne) {
            nd["x"] = nd["x"].get<float>() + nm2.ne->panOffsetX_ - this->panOffX;
            nd["y"] = nd["y"].get<float>() + nm2.ne->panOffsetY_ - this->panOffY;
        }
        auto* restored = nm2.addNodeNow(nd);
        if (!restored)
            throw std::runtime_error("RemoveNodeAction::undoAction: addNodeNow failed");
        for (auto c : connectionsData)
            nm2.makeNodeConnectionNow(c["srcNodeID"], c["srcConID"], c["dstNodeID"], c["dstConID"]);
    };
}

MakeNodeConnectionAction::MakeNodeConnectionAction(Project* p, std::vector<int> managerPath, int srcNodeID, int srcConID, int dstNodeID, int dstConID) :
        ProjectAction(p, MakeNodeConnection),
        managerPath(std::move(managerPath)),
        srcNodeID(srcNodeID),
        srcConID(srcConID),
        dstNodeID(dstNodeID),
        dstConID(dstConID) {
    name ="Connect Nodes";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.makeNodeConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.severConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
}

SeverNodeConnectionAction::SeverNodeConnectionAction(Project* p, std::vector<int> managerPath, int srcNodeID, int srcConID, int dstNodeID, int dstConID) :
        ProjectAction(p, SeverNodeConnection),
        managerPath(std::move(managerPath)),
        srcNodeID(srcNodeID),
        srcConID(srcConID),
        dstNodeID(dstNodeID),
        dstConID(dstConID) {
    name ="Sever Connection";
    doAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.severConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
    undoAction = [this] () {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.makeNodeConnectionNow(this->srcNodeID, this->srcConID, this->dstNodeID, this->dstConID);
    };
}

ReassignNodeConnectionAction::ReassignNodeConnectionAction(Project* p, std::vector<int> managerPath,
        const ConnIDs* oldCxns, int oldCount,
        int nSrcN, int nSrcC, int nDstN, int nDstC) :
        ProjectAction(p, ReassignNodeConnection),
        managerPath(std::move(managerPath)),
        oldConnCount(oldCount),
        newSrcNodeID(nSrcN), newSrcConID(nSrcC),
        newDstNodeID(nDstN), newDstConID(nDstC) {
    name = "Reassign Connection";
    for (int i = 0; i < oldCount; ++i) oldConns[i] = oldCxns[i];

    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        for (int i = 0; i < this->oldConnCount; ++i) {
            if (this->oldConns[i].existed)
                nm.severConnectionNow(static_cast<uint16_t>(this->oldConns[i].srcNodeID),
                                      static_cast<uint16_t>(this->oldConns[i].srcConID),
                                      static_cast<uint16_t>(this->oldConns[i].dstNodeID),
                                      static_cast<uint16_t>(this->oldConns[i].dstConID));
        }
        nm.makeNodeConnectionNow(static_cast<uint16_t>(this->newSrcNodeID),
                                 static_cast<uint16_t>(this->newSrcConID),
                                 static_cast<uint16_t>(this->newDstNodeID),
                                 static_cast<uint16_t>(this->newDstConID));
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        nm.severConnectionNow(static_cast<uint16_t>(this->newSrcNodeID),
                              static_cast<uint16_t>(this->newSrcConID),
                              static_cast<uint16_t>(this->newDstNodeID),
                              static_cast<uint16_t>(this->newDstConID));
        for (int i = this->oldConnCount - 1; i >= 0; --i) {
            if (this->oldConns[i].existed)
                nm.makeNodeConnectionNow(static_cast<uint16_t>(this->oldConns[i].srcNodeID),
                                         static_cast<uint16_t>(this->oldConns[i].srcConID),
                                         static_cast<uint16_t>(this->oldConns[i].dstNodeID),
                                         static_cast<uint16_t>(this->oldConns[i].dstConID));
        }
    };
}

IoPortChannelAction::IoPortChannelAction(Project* p, int opIn, std::vector<int> managerPathIn, uint16_t connectionIdIn, size_t connectionIndexIn) :
        ProjectAction(p, IoPortChannel),
        managerPath(std::move(managerPathIn)),
        op(opIn),
        connectionId(connectionIdIn),
        connectionIndex(connectionIndexIn) {
    switch (op) {
        case IoPortChannelOp::InputAddWaveform:
            name = "Add input bus waveform";
            break;
        case IoPortChannelOp::InputRemoveWaveform:
            name = "Remove input bus waveform";
            idAssigned = true;
            break;
        case IoPortChannelOp::InputAddEvent:
            name = "Add input bus event output";
            break;
        case IoPortChannelOp::InputRemoveEvent:
            name = "Remove input bus event output";
            idAssigned = true;
            break;
        case IoPortChannelOp::OutputAddWaveform:
            name = "Add output bus waveform";
            break;
        case IoPortChannelOp::OutputRemoveWaveform:
            name = "Remove output bus waveform";
            idAssigned = true;
            break;
        case IoPortChannelOp::OutputAddEvent:
            name = "Add output bus event input";
            break;
        case IoPortChannelOp::OutputRemoveEvent:
            name = "Remove output bus event input";
            idAssigned = true;
            break;
        default:
            name = "I/O port channel";
            break;
    }

    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        InputNode* in = nm.inNode;
        OutputNode* out = nm.outNode;
        switch (this->op) {
            case IoPortChannelOp::InputAddWaveform: {
                if (!this->idAssigned) {
                    const size_t pos = in->countWaveformOutputs();
                    in->addWaveformOutputChannel();
                    this->connectionId = in->outputs.connections[pos]->id;
                    this->connectionIndex = pos;
                    this->idAssigned = true;
                } else if (in->outputs.getConnection(this->connectionId) == nullptr) {
                    in->insertWaveformOutputChannelAt(this->connectionIndex, this->connectionId);
                }
                break;
            }
            case IoPortChannelOp::InputRemoveWaveform:
                in->removeWaveformOutputById(this->connectionId);
                break;
            case IoPortChannelOp::InputAddEvent: {
                if (!this->idAssigned) {
                    in->addEventOutputSocket();
                    this->connectionId = in->outputs.connections.back()->id;
                    this->connectionIndex = in->outputs.connections.size() - 1;
                    this->idAssigned = true;
                } else if (in->outputs.getConnection(this->connectionId) == nullptr) {
                    in->insertEventOutputChannelAt(this->connectionIndex, this->connectionId);
                }
                break;
            }
            case IoPortChannelOp::InputRemoveEvent:
                in->removeEventOutputById(this->connectionId);
                break;
            case IoPortChannelOp::OutputAddWaveform: {
                if (!this->idAssigned) {
                    const size_t pos = out->countWaveformInputs();
                    out->addWaveformInputChannel();
                    this->connectionId = out->inputs.connections[pos]->id;
                    this->connectionIndex = pos;
                    this->idAssigned = true;
                } else if (out->inputs.getConnection(this->connectionId) == nullptr) {
                    out->insertWaveformInputChannelAt(this->connectionIndex, this->connectionId);
                }
                break;
            }
            case IoPortChannelOp::OutputRemoveWaveform:
                out->removeWaveformInputById(this->connectionId);
                break;
            case IoPortChannelOp::OutputAddEvent: {
                if (!this->idAssigned) {
                    out->addEventInputSocket();
                    this->connectionId = out->inputs.connections.back()->id;
                    this->connectionIndex = out->inputs.connections.size() - 1;
                    this->idAssigned = true;
                } else if (out->inputs.getConnection(this->connectionId) == nullptr) {
                    out->insertEventInputChannelAt(this->connectionIndex, this->connectionId);
                }
                break;
            }
            case IoPortChannelOp::OutputRemoveEvent:
                out->removeEventInputById(this->connectionId);
                break;
            default:
                break;
        }
    };

    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        InputNode* in = nm.inNode;
        OutputNode* out = nm.outNode;
        switch (this->op) {
            case IoPortChannelOp::InputAddWaveform:
                in->removeWaveformOutputById(this->connectionId);
                break;
            case IoPortChannelOp::InputRemoveWaveform:
                if (in->outputs.getConnection(this->connectionId) == nullptr)
                    in->insertWaveformOutputChannelAt(this->connectionIndex, this->connectionId);
                break;
            case IoPortChannelOp::InputAddEvent:
                in->removeEventOutputById(this->connectionId);
                break;
            case IoPortChannelOp::InputRemoveEvent:
                if (in->outputs.getConnection(this->connectionId) == nullptr)
                    in->insertEventOutputChannelAt(this->connectionIndex, this->connectionId);
                break;
            case IoPortChannelOp::OutputAddWaveform:
                out->removeWaveformInputById(this->connectionId);
                break;
            case IoPortChannelOp::OutputRemoveWaveform:
                if (out->inputs.getConnection(this->connectionId) == nullptr)
                    out->insertWaveformInputChannelAt(this->connectionIndex, this->connectionId);
                break;
            case IoPortChannelOp::OutputAddEvent:
                out->removeEventInputById(this->connectionId);
                break;
            case IoPortChannelOp::OutputRemoveEvent:
                if (out->inputs.getConnection(this->connectionId) == nullptr)
                    out->insertEventInputChannelAt(this->connectionIndex, this->connectionId);
                break;
            default:
                break;
        }
    };
}

