#include "UndoManager.h"
#include "Node.h"
#include "NodeManager.h"
#include "Parameter.h"
#include "nodes/vst/vstnode.h"
#include <stdexcept>
#include "UndoInternal.h"

MapParameterUndoAction::MapParameterUndoAction(Project* p, std::vector<int> managerPath,
                                               int nodeID, size_t paramIndex, int vstParamID) :
    ProjectAction(p, MapParameter),
    managerPath(std::move(managerPath)),
    nodeID(nodeID),
    paramIndex(paramIndex),
    vstParamID(vstParamID) {
    name = "Map Parameter";
    doAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (this->vstParamID >= 0) {
            auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
            if (!vst) throw std::runtime_error("MapParameterUndoAction::doAction: VST node not found");
            vst->mapVstParameterNow(this->vstParamID);
            auto it = vst->mappedVstParams.find(this->vstParamID);
            if (it != vst->mappedVstParams.end()) {
                this->connectionID = it->second->id;
                this->idAssigned = true;
            }
        } else {
            Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                         : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                               : nm.getNode(static_cast<uint16_t>(this->nodeID));
            if (!target) throw std::runtime_error("MapParameterUndoAction::doAction: node not found");
            if (this->paramIndex >= target->params.size())
                throw std::runtime_error("MapParameterUndoAction::doAction: invalid paramIndex");
            target->mapParameterNow(this->paramIndex);
            Parameter* param = target->params[this->paramIndex];
            if (param && param->mappedConnection) {
                this->connectionID = param->mappedConnection->id;
                this->idAssigned = true;
            }
        }
    };
    undoAction = [this]() {
        NodeManager& nm = requireManager(this->p, this->managerPath);
        if (this->vstParamID >= 0) {
            auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(this->nodeID)));
            if (!vst) throw std::runtime_error("MapParameterUndoAction::undoAction: VST node not found");
            vst->unmapVstParameterNow(this->vstParamID);
        } else {
            Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm.outNode)
                         : (this->nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                               : nm.getNode(static_cast<uint16_t>(this->nodeID));
            if (!target) throw std::runtime_error("MapParameterUndoAction::undoAction: node not found");
            target->unmapParameterNow(this->paramIndex);
        }
    };
}

UnmapParameterUndoAction::UnmapParameterUndoAction(Project* p, std::vector<int> managerPath,
                                                   int nodeID, size_t paramIndex, int vstParamID) :
    ProjectAction(p, UnmapParameter),
    managerPath(std::move(managerPath)),
    nodeID(nodeID),
    paramIndex(paramIndex),
    vstParamID(vstParamID) {
    name = "Unmap Parameter";

    NodeManager& nm = requireManager(p, this->managerPath);
    if (vstParamID >= 0) {
        auto* vst = dynamic_cast<VstNode*>(nm.getNode(static_cast<uint16_t>(nodeID)));
        if (vst) {
            auto it = vst->mappedVstParams.find(vstParamID);
            if (it != vst->mappedVstParams.end()) {
                savedConnectionID = it->second->id;
                wasConnected = it->second->is_connected;
                if (wasConnected) {
                    srcNode = static_cast<uint16_t>(it->second->input_node);
                    srcCon = static_cast<uint16_t>(it->second->input_connection);
                }
            }
        }
    } else {
        Node* target = (nodeID == 0) ? static_cast<Node*>(nm.outNode)
                     : (nodeID == 1) ? static_cast<Node*>(nm.inNode)
                                     : nm.getNode(static_cast<uint16_t>(nodeID));
        if (target && paramIndex < target->params.size()) {
            Parameter* param = target->params[paramIndex];
            if (param && param->mappedConnection) {
                savedConnectionID = param->mappedConnection->id;
                wasConnected = param->mappedConnection->is_connected;
                if (wasConnected) {
                    srcNode = static_cast<uint16_t>(param->mappedConnection->input_node);
                    srcCon = static_cast<uint16_t>(param->mappedConnection->input_connection);
                }
            }
        }
    }

    doAction = [this]() {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        if (this->vstParamID >= 0) {
            auto* vst = dynamic_cast<VstNode*>(nm2.getNode(static_cast<uint16_t>(this->nodeID)));
            if (!vst) throw std::runtime_error("UnmapParameterUndoAction::doAction: VST node not found");
            vst->unmapVstParameterNow(this->vstParamID);
        } else {
            Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm2.outNode)
                         : (this->nodeID == 1) ? static_cast<Node*>(nm2.inNode)
                                               : nm2.getNode(static_cast<uint16_t>(this->nodeID));
            if (!target) throw std::runtime_error("UnmapParameterUndoAction::doAction: node not found");
            target->unmapParameterNow(this->paramIndex);
        }
    };
    undoAction = [this]() {
        NodeManager& nm2 = requireManager(this->p, this->managerPath);
        if (this->vstParamID >= 0) {
            auto* vst = dynamic_cast<VstNode*>(nm2.getNode(static_cast<uint16_t>(this->nodeID)));
            if (!vst) throw std::runtime_error("UnmapParameterUndoAction::undoAction: VST node not found");
            vst->mapVstParameterNow(this->vstParamID);
            auto it = vst->mappedVstParams.find(this->vstParamID);
            if (it != vst->mappedVstParams.end() && this->wasConnected)
                nm2.makeNodeConnectionNow(this->srcNode, this->srcCon,
                                          static_cast<uint16_t>(this->nodeID),
                                          it->second->id);
        } else {
            Node* target = (this->nodeID == 0) ? static_cast<Node*>(nm2.outNode)
                         : (this->nodeID == 1) ? static_cast<Node*>(nm2.inNode)
                                               : nm2.getNode(static_cast<uint16_t>(this->nodeID));
            if (!target) throw std::runtime_error("UnmapParameterUndoAction::undoAction: node not found");
            target->mapParameterNow(this->paramIndex);
            Parameter* param = target->params[this->paramIndex];
            if (param && param->mappedConnection && this->wasConnected)
                nm2.makeNodeConnectionNow(this->srcNode, this->srcCon,
                                          static_cast<uint16_t>(this->nodeID),
                                          param->mappedConnection->id);
        }
    };
}
