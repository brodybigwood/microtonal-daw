#include "NodeEditor.h"
#include "NodeManager.h"
#include "Node.h"
#include "ContextMenu.h"
#include "WindowHandler.h"
#include "PianoRoll.h"
#include "PianoRollWindow.h"
#include "UndoTreeExpandedWindow.h"
#include "PreferencesExpandedWindow.h"
#include "WindowManager.h"
#include "NodeProcessor.h"
#include "nodes/arranger/arranger.h"
#ifndef __EMSCRIPTEN__
#include "nodes/vst/vstplugin.h"
#endif
#include "UndoManager.h"
#include <iostream>
#include "Preferences.h"
#include "styles.h"
#include <algorithm>
#include <array>
#include <cmath>

// Patch-cable dragging, port hit-testing and wire rendering (split from NodeEditor.cpp).

void NodeEditor::renderPatchCable(SDL_Renderer* r, float x1, float y1, float x2, float y2, SDL_FColor color) {
    if (!r) return;

    const int segments = 32;
    const float thickness = 4.0f;

    SDL_Vertex verts[segments * 2];

    float x_prev = x1;
    float y_prev = y1;

    for (int i = 0; i < segments; i++) {
        float t = (float)i / (segments - 1);

        float y = y1 + (y2 - y1) * t;

        float s = (1 - cosf(t * static_cast<float>(M_PI))) / 2;
        float x_center = x1 + s * (x2 - x1);

        float nx, ny;
        if (i == 0) {
            nx = 0;
            ny = 1;
        } else {
            float dx = x_center - x_prev;
            float dy = y - y_prev;
            float len = sqrtf(dx * dx + dy * dy);
            if (len == 0.0f) len = 1.0f;
            nx = -dy / len;
            ny = dx / len;
        }
        float offset = thickness / 2.0f;

        verts[i * 2 + 0].position = {x_center + nx * offset, y + ny * offset};
        verts[i * 2 + 0].color = color;

        verts[i * 2 + 1].position = {x_center - nx * offset, y - ny * offset};
        verts[i * 2 + 1].color = color;

        x_prev = x_center;
        y_prev = y;
    }

    int indices[(segments - 1) * 6];
    for (int i = 0; i < segments - 1; i++) {
        int idx = i * 6;
        int v0 = i * 2;
        int v1 = i * 2 + 1;
        int v2 = (i + 1) * 2;
        int v3 = (i + 1) * 2 + 1;

        indices[idx + 0] = v0;
        indices[idx + 1] = v1;
        indices[idx + 2] = v2;
        indices[idx + 3] = v2;
        indices[idx + 4] = v1;
        indices[idx + 5] = v3;
    }

    SDL_RenderGeometry(r, nullptr, verts, segments * 2, indices, (segments - 1) * 6);
}

void NodeEditor::setDstConn(Node* node, int id) {
    dstNodeID = id;
    dstNode = node;

    makeConnection();
}

void NodeEditor::setSrcConn(Node* node, int id) {
    srcNodeID = id;
    srcNode = node;

    makeConnection();
}

void NodeEditor::startPortDrag(Node* node, int portId, Direction dir) {
    if (!node || !nm) return;

    Connection* conn = nullptr;
    if (dir == Direction::input) {
        conn = node->inputs.getConnection(static_cast<uint16_t>(portId));
    } else {
        conn = node->outputs.getConnection(static_cast<uint16_t>(portId));
    }

    if (!conn) return;

    dragNode_ = node;
    dragPort_ = portId;
    dragDir_ = dir;
    dragInProgress_ = true;

    // Cancel any pending click-click connection in progress
    srcNode = nullptr;
    srcNodeID = -1;
    dstNode = nullptr;
    dstNodeID = -1;

    if (conn->is_connected) {
        dragIsNew_ = false;
        draggedConnection_ = conn;

        if (dir == Direction::input) {
            // On an input connection, the upstream output is in input_node/input_connection.
            dragAnchorNode_ = nm->getNode(static_cast<uint16_t>(conn->input_node));
            dragAnchorPort_ = conn->input_connection;
        } else {
            // On an output connection, the downstream input is in output_node/output_connection.
            dragAnchorNode_ = nm->getNode(static_cast<uint16_t>(conn->output_node));
            dragAnchorPort_ = conn->output_connection;
        }
    } else {
        dragIsNew_ = true;
        draggedConnection_ = nullptr;
        dragAnchorNode_ = nullptr;
        dragAnchorPort_ = -1;
    }
}

void NodeEditor::startNodeDrag(Node* node) {
    if (!node) return;
    movingNode = node;
    node->moving = true;
    movingNodeStartX = node->dstRect.x;
    movingNodeStartY = node->dstRect.y;
    SDL_GetMouseState(&moveOffX, &moveOffY);
}

void NodeEditor::stopNodeDrag() {
    if (movingNode) {
        movingNode->moving = false;
        movingNode = nullptr;
    }
}

Node* NodeEditor::findPortAt(float mx, float my, int& portId, Direction& portDir) {
    if (!nm) return nullptr;

    auto checkNode = [&](Node* n) -> Node* {
        if (!n || !n->visible || !n->showConnectionPorts()) return nullptr;
        for (auto* c : n->inputs.connections) {
            if (c && mx >= c->rect.x && mx <= c->rect.x + c->rect.w &&
                my >= c->rect.y && my <= c->rect.y + c->rect.h) {
                portId = c->id;
                portDir = Direction::input;
                return n;
            }
        }
        for (auto* c : n->outputs.connections) {
            if (c && mx >= c->rect.x && mx <= c->rect.x + c->rect.w &&
                my >= c->rect.y && my <= c->rect.y + c->rect.h) {
                portId = c->id;
                portDir = Direction::output;
                return n;
            }
        }
        return nullptr;
    };

    if (Node* found = checkNode(nm->inNode)) return found;
    if (Node* found = checkNode(nm->outNode)) return found;
    for (auto n : nm->getNodes()) {
        if (Node* found = checkNode(n)) return found;
    }
    return nullptr;
}

void NodeEditor::handleDragDrop() {
    if (!dragInProgress_) return;

    int dropPortId = -1;
    Direction dropPortDir = Direction::input;
    Node* dropNode = findPortAt(mouseX, mouseY, dropPortId, dropPortDir);

    bool shouldSever = false;
    bool shouldConnect = false;
    Node* srcNode = nullptr;
    uint16_t srcId = 0;
    Node* dstNode = nullptr;
    uint16_t dstId = 0;

    if (!dropNode) {
        shouldSever = !dragIsNew_ && draggedConnection_;
    } else {
        bool dropOnSamePort = dropNode == dragNode_ && dropPortId == dragPort_ && dropPortDir == dragDir_;
        bool dropOnAnchor = !dragIsNew_ && dropNode == dragAnchorNode_ && dropPortId == dragAnchorPort_;

        if (!dropOnSamePort && !dropOnAnchor) {
            // Collect old connections to sever and the new connection to make,
            // then execute as a single undoable action.
            ConnIDs oldCxns[2];
            int oldCount = 0;

            // Sever existing connection on the drop target.
            Connection* dropConn = nullptr;
            if (dropPortDir == Direction::input)
                dropConn = dropNode->inputs.getConnection(static_cast<uint16_t>(dropPortId));
            else
                dropConn = dropNode->outputs.getConnection(static_cast<uint16_t>(dropPortId));
            if (dropConn && dropConn->is_connected && dropConn != draggedConnection_) {
                if (dropConn->dir == Direction::input) {
                    oldCxns[oldCount].srcNodeID = dropConn->input_node;
                    oldCxns[oldCount].srcConID = dropConn->input_connection;
                    oldCxns[oldCount].dstNodeID = dropConn->output_node;
                    oldCxns[oldCount].dstConID = dropConn->output_connection;
                } else {
                    // Find which node owns this output connection.
                    oldCxns[oldCount].srcNodeID = dropNode->id;
                    oldCxns[oldCount].srcConID = dropConn->id;
                    oldCxns[oldCount].dstNodeID = dropConn->output_node;
                    oldCxns[oldCount].dstConID = dropConn->output_connection;
                }
                oldCxns[oldCount].existed = true;
                ++oldCount;
            }

            // Sever the dragged existing connection.
            if (!dragIsNew_ && draggedConnection_) {
                if (draggedConnection_->dir == Direction::input) {
                    oldCxns[oldCount].srcNodeID = draggedConnection_->input_node;
                    oldCxns[oldCount].srcConID = draggedConnection_->input_connection;
                    oldCxns[oldCount].dstNodeID = draggedConnection_->output_node;
                    oldCxns[oldCount].dstConID = draggedConnection_->output_connection;
                } else {
                    oldCxns[oldCount].srcNodeID = dragNode_->id;
                    oldCxns[oldCount].srcConID = draggedConnection_->id;
                    oldCxns[oldCount].dstNodeID = draggedConnection_->output_node;
                    oldCxns[oldCount].dstConID = draggedConnection_->output_connection;
                }
                oldCxns[oldCount].existed = true;
                ++oldCount;
            }

            // Determine src (output) and dst (input) for the new connection.
            if (dragIsNew_) {
                if (dragDir_ != dropPortDir) {
                    shouldConnect = true;
                    if (dragDir_ == Direction::output) {
                        srcNode = dragNode_; srcId = static_cast<uint16_t>(dragPort_);
                        dstNode = dropNode; dstId = static_cast<uint16_t>(dropPortId);
                    } else {
                        srcNode = dropNode; srcId = static_cast<uint16_t>(dropPortId);
                        dstNode = dragNode_; dstId = static_cast<uint16_t>(dragPort_);
                    }
                }
            } else if (dragDir_ != dropPortDir) {
                shouldConnect = true;
                if (dragDir_ == Direction::output) {
                    srcNode = dragNode_; srcId = static_cast<uint16_t>(dragPort_);
                    dstNode = dropNode; dstId = static_cast<uint16_t>(dropPortId);
                } else {
                    srcNode = dropNode; srcId = static_cast<uint16_t>(dropPortId);
                    dstNode = dragNode_; dstId = static_cast<uint16_t>(dragPort_);
                }
            } else {
                shouldConnect = true;
                if (dragDir_ == Direction::output) {
                    srcNode = dropNode; srcId = static_cast<uint16_t>(dropPortId);
                    dstNode = dragAnchorNode_; dstId = static_cast<uint16_t>(dragAnchorPort_);
                } else {
                    srcNode = dragAnchorNode_; srcId = static_cast<uint16_t>(dragAnchorPort_);
                    dstNode = dropNode; dstId = static_cast<uint16_t>(dropPortId);
                }
            }

            if (shouldConnect) {
                auto* ra = new ReassignNodeConnectionAction(
                    nm->project, nm->managerPath,
                    oldCxns, oldCount,
                    static_cast<int>(srcNode->id), static_cast<int>(srcId),
                    static_cast<int>(dstNode->id), static_cast<int>(dstId));
                nm->project->um->newAction(ra);
            }
        }
    }

    if (shouldSever && !shouldConnect)
        nm->severConnection(draggedConnection_);

    dragInProgress_ = false;
    dragIsNew_ = false;
    dragNode_ = nullptr;
    dragPort_ = -1;
    dragAnchorNode_ = nullptr;
    dragAnchorPort_ = -1;
    draggedConnection_ = nullptr;
}

void NodeEditor::makeConnection() {
    // srcNodeID & dstNodeID are the connection/io ids, not node identifiers
    if(srcNode != nullptr && dstNode != nullptr
            && srcNodeID != -1 && dstNodeID != -1 ) {
        nm->makeNodeConnection(
                srcNode, srcNodeID,
                dstNode, dstNodeID );
        srcNode = nullptr;
        srcNodeID = -1;
        dstNode = nullptr;
        dstNodeID = -1;
    }
}

bool NodeEditor::isConnectionBeingDragged(Connection* c) const {
    if (!dragInProgress_ || dragIsNew_) return false;
    if (c == draggedConnection_) return true;
    // When dragging from an output, the cable is rendered by the anchor's input connection.
    if (dragDir_ == Direction::output && dragAnchorNode_ && dragAnchorPort_ >= 0) {
        Connection* anchorConn = dragAnchorNode_->inputs.getConnection(static_cast<uint16_t>(dragAnchorPort_));
        if (c == anchorConn) return true;
    }
    return false;
}

void NodeEditor::clearPointersToNode(Node* n) {
    if (!n) return;
    if (hoveredNode == n) hoveredNode = nullptr;
    if (dstNode == n) {
        dstNode = nullptr;
        dstNodeID = -1;
    }
    if (srcNode == n) {
        srcNode = nullptr;
        srcNodeID = -1;
    }
    // Close any expanded windows that reference this node.
    if (nm && nm->project && nm->project->processor) {
        auto* wm = nm->project->processor->getWindowManager();
        if (wm) {
            wm->removeWindowsByTitleSuffix("##" + std::to_string(n->id));
        }
    }
    if (movingNode == n) {
        movingNode->moving = false;
        movingNode = nullptr;
    }
    if (dragNode_ == n || dragAnchorNode_ == n) {
        dragInProgress_ = false;
        dragIsNew_ = false;
        dragNode_ = nullptr;
        dragPort_ = -1;
        dragAnchorNode_ = nullptr;
        dragAnchorPort_ = -1;
        draggedConnection_ = nullptr;
    }
}

void NodeEditor::clearWireDragState() {
    if (movingNode) {
        movingNode->moving = false;
        movingNode = nullptr;
    }
    hoveredNode = nullptr;
    dstNode = nullptr;
    dstNodeID = -1;
    srcNode = nullptr;
    srcNodeID = -1;
    dragInProgress_ = false;
    dragIsNew_ = false;
    dragNode_ = nullptr;
    dragPort_ = -1;
    dragAnchorNode_ = nullptr;
    dragAnchorPort_ = -1;
    draggedConnection_ = nullptr;
}

void NodeEditor::renderConnector(SDL_Renderer* renderer) {

    float x, y;
    DataType dtype = DataType::Waveform;

    if (dragInProgress_) {
        Connection* conn = nullptr;
        if (dragIsNew_) {
            // New drag: fixed end is the clicked port.
            if (dragDir_ == Direction::output) {
                conn = dragNode_->outputs.getConnection(static_cast<uint16_t>(dragPort_));
            } else {
                conn = dragNode_->inputs.getConnection(static_cast<uint16_t>(dragPort_));
            }
        } else {
            // Existing drag: fixed end is the anchor.
            if (dragDir_ == Direction::input) {
                // Dragging input — anchor is the upstream output.
                conn = dragAnchorNode_->outputs.getConnection(static_cast<uint16_t>(dragAnchorPort_));
            } else {
                // Dragging output — anchor is the downstream input.
                conn = dragAnchorNode_->inputs.getConnection(static_cast<uint16_t>(dragAnchorPort_));
            }
        }
        if (!conn) return;
        auto rect = conn->rect;
        x = rect.x + rect.w/2.0f;
        y = rect.y + rect.h/2.0f;
        dtype = conn->type;
    } else {
        return;
    }

    SDL_FColor color;
    if (dtype == DataType::Events)
        color = {colors.trackNotes[0]/255.f, colors.trackNotes[1]/255.f, colors.trackNotes[2]/255.f, 1.0f};
    else
        color = {colors.trackAudio[0]/255.f, colors.trackAudio[1]/255.f, colors.trackAudio[2]/255.f, 1.0f};
    renderSine(renderer, mouseX, mouseY, x, y, color);
}

void NodeEditor::renderSine(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, SDL_FColor color) {
    renderPatchCable(renderer, x1, y1, x2, y2, color);
}

void NodeEditor::clearStaleHover() {
    if (!nm) return;
    stopNodeDrag();
    leftClick = false;
    panning_ = false;
    // Release any captured embedded window.
    if (capturedEmbeddedWindow_) {
        capturedEmbeddedWindow_->captured_ = false;
        capturedEmbeddedWindow_ = nullptr;
        captureKind_ = CaptureKind::None;
    }
    for (auto* n : nm->getNodes()) {
        n->hoveredConnection = -1;
        n->captured_ = false;
    }
    nm->inNode->hoveredConnection = -1;
    nm->inNode->captured_ = false;
    nm->outNode->hoveredConnection = -1;
    nm->outNode->captured_ = false;
    focusedEmbeddedWindow_ = nullptr;
    hoveredNode = nullptr;
    dragInProgress_ = false;
    dragNode_ = nullptr;
}

