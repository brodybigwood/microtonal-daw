#include "NodeEditor.h"
#include "NodeManager.h"
#include "Node.h"
#include "ContextMenu.h"
#include "WindowHandler.h"
#include "PianoRoll.h"
#include "nodes/arranger/arranger.h"
#ifndef __EMSCRIPTEN__
#include "nodes/vst/vstplugin.h"
#endif
#include "UndoManager.h"
#include <iostream>
#include "Preferences.h"
#include "PreferencesWindow.h"
#include "UndoTreeWindow.h"
#include "styles.h"
#include <algorithm>
#include <array>
#include <cmath>

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

void NodeEditor::retach() {
    for (auto n : nm->getNodes()) {
        n->attach();
        n->clearTextures();
    }
    nm->inNode->attach();
    nm->inNode->clearTextures();
    nm->outNode->attach();
    nm->outNode->clearTextures();
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

NodeEditor::NodeEditor() :
    isAltPressed(WindowHandler::instance()->isAltPressed),
    isCtrlPressed(WindowHandler::instance()->isCtrlPressed),
    isShiftPressed(WindowHandler::instance()->isShiftPressed) {
}

NodeEditor::~NodeEditor() {
    if (currentCursor_) {
        SDL_DestroyCursor(currentCursor_);
        currentCursor_ = nullptr;
    }
}

void NodeEditor::setEmbeddedCanvasSize(float w, float h) {
    canvasW_ = w;
    canvasH_ = h;
    nodeRect = SDL_FRect{0.f, 0.f, w, h};
}

void NodeEditor::resetRootMenuBarLayout() {
    topMargin = 0.f;
    nodeRect = SDL_FRect{0.f, 0.f, canvasW_, canvasH_};
}

void NodeEditor::updateRootMenuBarLayout() {
    resetRootMenuBarLayout();
    if (rootMenuBar_) {
        topMargin = kRootMenuBarStripH;
        nodeRect = SDL_FRect{0.f, topMargin, canvasW_, std::max(1.f, canvasH_ - topMargin)};
        return;
    }
    if (!nm || !menuBarHostNode_) return;
    if (!menuBarHostNode_->nm || !menuBarHostNode_->nm->managerPath.empty()) return;
    topMargin = kRootMenuBarStripH;
    nodeRect = SDL_FRect{0.f, topMargin, canvasW_, std::max(1.f, canvasH_ - topMargin)};
}

bool NodeEditor::isPointerOverMenuBar(float mx, float my) const {
    if (topMargin <= 0.f) return false;
    return mx >= 0.f && mx < canvasW_ && my >= 0.f && my < topMargin;
}

void NodeEditor::renderRootMenuBarSkeleton(SDL_Renderer* ren, const SDL_FRect* surface) {
    auto fill = [ren](uint8_t R, uint8_t G, uint8_t B, const SDL_FRect& r) {
        SDL_SetRenderDrawColor(ren, R, G, B, 255);
        SDL_RenderFillRect(ren, &r);
    };

    fill(38, 38, 40, SDL_FRect{surface->x, surface->y, surface->w, topMargin});

    static constexpr std::array<const char*, 4> kLabels{"File", "Edit", "View", "Window"};
    constexpr float kPadX = 6.f;
    constexpr float kMinItemW = 52.f;
    float x = surface->x + kPadX;
    const float y = surface->y + 2.f;
    const float h = topMargin - 4.f;

    for (size_t i = 0; i < kLabels.size(); ++i) {
        int tw = 0;
        int th = 0;
        SDL_Surface* surf = nullptr;
        if (fonts.mainFont)
            surf = TTF_RenderText_Blended(fonts.mainFont, kLabels[i], 0, SDL_Color{255, 255, 255, 255});
        if (surf) {
            tw = surf->w;
            th = surf->h;
        }
        const float w = std::max(kMinItemW, static_cast<float>(tw) + 16.f);
        const SDL_FRect cell{x, y, w, h};

        // Store for hit-testing in handleInput.
        menuLabelRects_[i] = cell;

        // Highlight the open menu label.
        if (static_cast<int>(i) == menuOpenIndex_)
            fill(58, 58, 64, cell);
        else
            fill(44, 44, 48, cell);

        SDL_SetRenderDrawColor(ren, 20, 20, 22, 255);
        SDL_RenderRect(ren, &cell);

        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
            SDL_DestroySurface(surf);
            if (tex) {
                const float tx = x + (w - static_cast<float>(tw)) * 0.5f;
                const float ty = y + (h - static_cast<float>(th)) * 0.5f;
                SDL_FRect tr{tx, ty, static_cast<float>(tw), static_cast<float>(th)};
                SDL_RenderTexture(ren, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
        }
        x += w + 4.f;
    }
}

std::shared_ptr<TreeEntry> NodeEditor::buildMenuTree(int menuIndex) {
    auto root = uTreeEntry();
    switch (menuIndex) {
        case 1: { // Edit
            root->label = "Edit";
            {
                auto item = uTreeEntry();
                item->label = "Preferences...";
                item->click = [this, w = canvasW_, h = canvasH_]() {
                    auto existing = existingPreferencesWindow();
                    if (!existing) {
                        auto pw = std::make_unique<PreferencesWindow>();
                        pw->id = 0;
                        existing = pw.get();
                        addEmbeddedWindow(std::move(pw));
                        existing->moveTo((w - existing->w) * 0.5f, (h - existing->h) * 0.4f);
                    }
                    int maxZ = 0;
                    for (auto& ew : embeddedWindows_)
                        if (ew->zOrder > maxZ) maxZ = ew->zOrder;
                    existing->zOrder = maxZ + 1;
                    existing->open();
                };
                root->addChild(item);
            }
            {
                auto item = uTreeEntry();
                item->label = "Undo Tree";
                item->click = [this, w = canvasW_, h = canvasH_]() {
                    auto existing = existingUndoTreeWindow();
                    if (!existing) {
                        auto uw = std::make_unique<UndoTreeWindow>(nm->project);
                        uw->id = 1;
                        existing = uw.get();
                        addEmbeddedWindow(std::move(uw));
                        existing->moveTo((w - existing->w) * 0.5f, (h - existing->h) * 0.4f);
                    }
                    int maxZ = 0;
                    for (auto& ew : embeddedWindows_)
                        if (ew->zOrder > maxZ) maxZ = ew->zOrder;
                    existing->zOrder = maxZ + 1;
                    existing->open();
                };
                root->addChild(item);
            }
            break;
        }
        default: break;
    }
    return root;
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
    if (dtype == DataType::Events) color = {0.5f, 1.0f, 0.5f, 1.0f};
    else color = {1.0f, 0.5f, 0.5f, 1.0f};
    renderSine(renderer, mouseX, mouseY, x, y, color);
}

void NodeEditor::renderSine(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, SDL_FColor color) {
    renderPatchCable(renderer, x1, y1, x2, y2, color);
}

void NodeEditor::tick(SDL_Renderer* r) {
    if (!nm || !r) return;
    SDL_FRect surface{0.f, 0.f, canvasW_, canvasH_};
    render(r, &surface);
}

void NodeEditor::renderPresent(SDL_Renderer* renderer) {
    for (auto n : nm->getNodes()) {
        n->renderPresent();
    }
    nm->inNode->renderPresent();
    if (renderer) SDL_RenderPresent(renderer);
}

void NodeEditor::move() {
    auto x = mouseX - moveOffX;
    auto y = mouseY - moveOffY;

    panOffsetX_ += x;
    panOffsetY_ += y;

    auto moveNode = [x, y] (Node* n) {
        n->move(n->dstRect.x + x, n->dstRect.y + y);
    };

    for (auto n : nm->getNodes()) moveNode(n);
    moveNode(nm->inNode);
    moveNode(nm->outNode);

    moveOffX = mouseX;
    moveOffY = mouseY;
}

void NodeEditor::zoom(float amount) {
    for (auto n : nm->getNodes()) if (!n->canZoom(amount)) return;
    if (!nm->inNode->canZoom(amount)) return;
    if (!nm->outNode->canZoom(amount)) return;
    
    auto zoomNode = [this, amount] (Node* n) {        

        float mx = (n->dstRect.x - mouseX) * amount + mouseX;
        float my = (n->dstRect.y - mouseY) * amount + mouseY;
        
        n->zoom(amount);
        n->move(mx, my);
    };

    for (auto n : nm->getNodes()) zoomNode(n);
    zoomNode(nm->inNode);
    zoomNode(nm->outNode);
}

void NodeEditor::handleInput(SDL_Event& e) {
#ifndef __EMSCRIPTEN__
    VstPlugin::tickAllEditors();
#endif
    moveMouse();

    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (dragInProgress_) {
            if (capturedEmbeddedWindow_) {
                capturedEmbeddedWindow_->captured_ = false;
                capturedEmbeddedWindow_ = nullptr;
                captureKind_ = CaptureKind::None;
            }
            handleDragDrop();
            return;
        }
        if (panning_ && !rootMenuBar_) {
            float dx = mouseX - panStartX_;
            float dy = mouseY - panStartY_;
            if (dx != 0.f || dy != 0.f) {
                nm->project->um->newAction(
                    new PanNodesAction(nm->project, nm->managerPath, dx, dy));
            }
        }
        panning_ = false;
        leftClick = false;
    }

    if (routeEmbeddedWindowEvent(e, mouseX, mouseY))
        return;

    if (topMargin > 0.f) {
        // Sync: detect when ContextMenu closed the dropdown (click outside, leaf click, etc.).
        if (menuOpenIndex_ >= 0 && !ContextMenu::get()->active)
            menuOpenIndex_ = -1;

        const bool overMenuBar = isPointerOverMenuBar(mouseX, mouseY);
        const bool menuIsOpen = menuOpenIndex_ >= 0;

        if (overMenuBar || menuIsOpen) {
            // Left-click on a menu label when no menu is open: open the dropdown via ContextMenu.
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && overMenuBar && !menuIsOpen) {
                int clickedLabel = -1;
                for (size_t i = 0; i < menuLabelRects_.size(); ++i) {
                    if (mouseX >= menuLabelRects_[i].x && mouseX < menuLabelRects_[i].x + menuLabelRects_[i].w &&
                        mouseY >= menuLabelRects_[i].y && mouseY < menuLabelRects_[i].y + menuLabelRects_[i].h) {
                        clickedLabel = static_cast<int>(i);
                        break;
                    }
                }
                if (clickedLabel >= 0) {
                    auto tree = buildMenuTree(clickedLabel);
                    if (tree && !tree->children.empty()) {
                        auto* ctx = ContextMenu::get();
                        ctx->activate();
                        ctx->skipNextEvent = true;
                        ctx->locX = menuLabelRects_[static_cast<size_t>(clickedLabel)].x;
                        ctx->locY = topMargin;
                        ctx->dynamicTick = getTreeMenuTicker(tree);
                        menuOpenIndex_ = clickedLabel;
                    }
                }
                return;
            }

            // Consume all mouse events while pointer is over menu bar or a menu is open.
            switch (e.type) {
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                case SDL_EVENT_MOUSE_MOTION:
                case SDL_EVENT_MOUSE_WHEEL:
                    return;
                default:
                    break;
            }
        }
    }

    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (isCtrlPressed && leftClick) move();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            leftClick = true;
            moveOffX = mouseX;
            moveOffY = mouseY;
            if (isCtrlPressed && !rootMenuBar_) {
                panStartX_ = mouseX;
                panStartY_ = mouseY;
                panning_ = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (isCtrlPressed) {
                float amt = std::pow(1.1f, e.wheel.y);
                zoom(amt);
                if (!rootMenuBar_) {
                    auto* um = nm->project->um;
                    if (um->current->type == ZoomNodes) {
                        auto* prev = static_cast<ZoomNodesAction*>(um->current);
                        if (prev->managerPath == nm->managerPath) {
                            prev->addStep(amt, mouseX, mouseY);
                            ProjectAction* cap = prev;
                            um->enqueueAudioSync([cap]() { cap->doAction(); });
                        } else {
                            um->newAction(new ZoomNodesAction(nm->project, nm->managerPath, amt, mouseX, mouseY));
                        }
                    } else {
                        um->newAction(new ZoomNodesAction(nm->project, nm->managerPath, amt, mouseX, mouseY));
                    }
                }
            }
            break;
        default:
            break;
    }
    switch(e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            clickMouse(e);
            break;
        case SDL_EVENT_KEY_DOWN:
            keydown(e);
        default:
            break;
    }
}

void NodeEditor::moveMouse() {
    if (!embedded_)
        SDL_GetMouseState(&mouseX, &mouseY);

    if (movingNode) movingNode->move(mouseX - moveOffX, mouseY - moveOffY);
    hover();
}

bool NodeEditor::inside(float& mouseX, float& mouseY, SDL_FRect* rect) {
    return (
        mouseX > rect->x &&
        mouseX < rect->x + rect->w &&
        mouseY > rect->y &&
        mouseY < rect->y + rect->h
    );
}

void NodeEditor::hover() {

}

void NodeEditor::clickMouse(SDL_Event& e) {
    if (e.button.button == SDL_BUTTON_LEFT) {
        auto time = SDL_GetTicks();
        auto interval = time - lastLeftClick;
        lastLeftClick = time;
        if(interval < DCT) {
            doubleClick();
            return;
        }
    
        makeConnection();
        srcNodeID = -1;
        dstNodeID = -1;
        srcNode = nullptr;
        dstNode = nullptr;

    } else if (e.button.button == SDL_BUTTON_RIGHT) {
        auto* ctxMenu = ContextMenu::get();
        ctxMenu->activate();

        auto t = getClickMenu();

        ctxMenu->dynamicTick = getTreeMenuTicker(t);         
    }
}
void NodeEditor::doubleClick() {
    //createNode();
}

std::shared_ptr<TreeEntry> NodeEditor::getClickMenu() {
    auto t = uTreeEntry();
    t->label = "Menu";

    auto create = uTreeEntry();
    create->label = "Add Node";
    
    // create buttons for all node types

    for (int i = 0; i < NodeType::Count; ++i) {
        auto nodeType = uTreeEntry();
        nodeType->label = NodeTypeStr[i];
        nodeType->click = [this, i]() { createNode(static_cast<NodeType>(i)); };

        create->addChild(nodeType);
    }   
    
    t->addChild(create);

    return t;
}

void NodeEditor::createNode(NodeType t) {
    nm->addNode(t, mouseX, mouseY);
}

void NodeEditor::keydown(SDL_Event& e) {

}

// --- Embedded window management ---

EmbeddedWindow* NodeEditor::addEmbeddedWindow(std::unique_ptr<EmbeddedWindow> w) {
    if (!w) return nullptr;
    int maxZ = 0;
    for (auto& ew : embeddedWindows_)
        if (ew->id != 0 && ew->id != 1 && ew->zOrder > maxZ) maxZ = ew->zOrder;
    if (w->id == 0 || w->id == 1) {
        for (auto& ew : embeddedWindows_)
            if (ew->zOrder > maxZ) maxZ = ew->zOrder;
    }
    w->zOrder = maxZ + 1;
    registerEmbeddedWindow(w.get());
    EmbeddedWindow* ptr = w.get();
    embeddedWindows_.push_back(std::move(w));
    return ptr;
}

void NodeEditor::clearPointersToEmbeddedWindow(EmbeddedWindow* w) {
    if (!w) return;
    if (capturedEmbeddedWindow_ == w) capturedEmbeddedWindow_ = nullptr;
    if (focusedEmbeddedWindow_ == w) focusedEmbeddedWindow_ = nullptr;
}

void NodeEditor::removeEmbeddedWindow(EmbeddedWindow* w) {
    if (!w) return;
    clearPointersToEmbeddedWindow(w);
    unregisterEmbeddedWindow(w);
    for (auto it = embeddedWindows_.begin(); it != embeddedWindows_.end(); ++it) {
        if (it->get() == w) {
            embeddedWindows_.erase(it);
            return;
        }
    }
}

PreferencesWindow* NodeEditor::existingPreferencesWindow() {
    for (auto& ew : embeddedWindows_) {
        if (auto* pw = dynamic_cast<PreferencesWindow*>(ew.get()))
            return pw;
    }
    return nullptr;
}

UndoTreeWindow* NodeEditor::existingUndoTreeWindow() {
    for (auto& ew : embeddedWindows_) {
        if (auto* uw = dynamic_cast<UndoTreeWindow*>(ew.get()))
            return uw;
    }
    return nullptr;
}

void NodeEditor::renderEmbeddedWindows(SDL_Renderer* r) {
    std::vector<EmbeddedWindow*> sorted;
    sorted.reserve(embeddedWindows_.size());
    for (auto& ew : embeddedWindows_)
        if (ew->visible) sorted.push_back(ew.get());
    std::sort(sorted.begin(), sorted.end(),
              [](EmbeddedWindow* a, EmbeddedWindow* b) { return a->zOrder < b->zOrder; });
    for (auto* ew : sorted)
        ew->render(r);
}

bool NodeEditor::routeEmbeddedWindowEvent(SDL_Event& e, float mouseX, float mouseY) {
    EmbeddedWindow* target = nullptr;
    bool targetIsResize = false;
    EmbeddedWindow* prevCapture = capturedEmbeddedWindow_;

    // --- Mouseup: route to captured window, release after processing ---
    bool releaseCaptureAfter = false;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        target = capturedEmbeddedWindow_;
        targetIsResize = (captureKind_ == CaptureKind::Resize);
        releaseCaptureAfter = (capturedEmbeddedWindow_ != nullptr);
    }

    // --- Motion / other events during capture: route to same window ---
    if (!target && capturedEmbeddedWindow_) {
        target = capturedEmbeddedWindow_;
        targetIsResize = (captureKind_ == CaptureKind::Resize);
    }

    // --- No target: search non-node windows first (always on top), then nodes ---
    if (!target) {
        auto searchInList = [&](std::vector<EmbeddedWindow*>& list) -> bool {
            std::sort(list.begin(), list.end(),
                      [](EmbeddedWindow* a, EmbeddedWindow* b) { return a->zOrder > b->zOrder; });
            for (auto* ew : list) {
                if (ew->getResizeZone(mouseX, mouseY) != EmbeddedWindow::ResizeZone::None) {
                    target = ew;
                    targetIsResize = true;
                    captureKind_ = CaptureKind::Resize;
                    return true;
                }
                if (ew->hitTest(mouseX, mouseY)) {
                    target = ew;
                    captureKind_ = CaptureKind::Content;
                    return true;
                }
                Node* node = dynamic_cast<Node*>(ew);
                if (node && node->showConnectionPorts()) {
                    for (auto* c : node->inputs.connections) {
                        if (c && mouseX >= c->rect.x && mouseX <= c->rect.x + c->rect.w &&
                            mouseY >= c->rect.y && mouseY <= c->rect.y + c->rect.h) { target = node; break; }
                    }
                    if (!target) {
                        for (auto* c : node->outputs.connections) {
                            if (c && mouseX >= c->rect.x && mouseX <= c->rect.x + c->rect.w &&
                                mouseY >= c->rect.y && mouseY <= c->rect.y + c->rect.h) { target = node; break; }
                        }
                    }
                    if (target) {
                        captureKind_ = CaptureKind::Connection;
                        return true;
                    }
                }
            }
            return false;
        };

        // Non-node windows first (undo tree, prefs — rendered on top, get input priority).
        {
            std::vector<EmbeddedWindow*> winList;
            for (auto& ew : embeddedWindows_)
                if (ew->visible) winList.push_back(ew.get());
            if (searchInList(winList)) goto found;
        }
        // Nodes second.
        if (nm) {
            std::vector<EmbeddedWindow*> nodeList;
            for (auto n : nm->getNodes())
                if (n->visible) nodeList.push_back(n);
            if (nm->inNode && nm->inNode->visible) nodeList.push_back(nm->inNode);
            if (nm->outNode && nm->outNode->visible) nodeList.push_back(nm->outNode);
            if (searchInList(nodeList)) goto found;
        }
        found:;

        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && target) {
            // Release previous capture if any
            if (capturedEmbeddedWindow_ && capturedEmbeddedWindow_ != target) {
                capturedEmbeddedWindow_->captured_ = false;
            }
            capturedEmbeddedWindow_ = target;
            target->captured_ = true;
            focusedEmbeddedWindow_ = target;
            int maxZ = 0;
            bool targetIsSystem = (target->id == 0 || target->id == 1);
            for (auto& ewin : embeddedWindows_) {
                if (!targetIsSystem && (ewin->id == 0 || ewin->id == 1)) continue;
                if (ewin->zOrder > maxZ) maxZ = ewin->zOrder;
            }
            if (nm) {
                for (auto n : nm->getNodes()) if (n->zOrder > maxZ) maxZ = n->zOrder;
                if (nm->inNode && nm->inNode->zOrder > maxZ) maxZ = nm->inNode->zOrder;
                if (nm->outNode && nm->outNode->zOrder > maxZ) maxZ = nm->outNode->zOrder;
            }
            target->zOrder = maxZ + 1;
        }
    }

    // --- Cursor ---
    {
        SDL_SystemCursor cur = SDL_SYSTEM_CURSOR_DEFAULT;
        if (target && targetIsResize) {
            auto zone = target->getResizeZone(mouseX, mouseY);
            switch (zone) {
                case EmbeddedWindow::ResizeZone::N:  cur = SDL_SYSTEM_CURSOR_N_RESIZE;  break;
                case EmbeddedWindow::ResizeZone::S:  cur = SDL_SYSTEM_CURSOR_S_RESIZE;  break;
                case EmbeddedWindow::ResizeZone::E:  cur = SDL_SYSTEM_CURSOR_E_RESIZE;  break;
                case EmbeddedWindow::ResizeZone::W:  cur = SDL_SYSTEM_CURSOR_W_RESIZE;  break;
                case EmbeddedWindow::ResizeZone::NE: cur = SDL_SYSTEM_CURSOR_NE_RESIZE; break;
                case EmbeddedWindow::ResizeZone::NW: cur = SDL_SYSTEM_CURSOR_NW_RESIZE; break;
                case EmbeddedWindow::ResizeZone::SE: cur = SDL_SYSTEM_CURSOR_SE_RESIZE; break;
                case EmbeddedWindow::ResizeZone::SW: cur = SDL_SYSTEM_CURSOR_SW_RESIZE; break;
                default: break;
            }
        }
        if (currentCursor_) SDL_DestroyCursor(currentCursor_);
        currentCursor_ = SDL_CreateSystemCursor(cur);
        SDL_SetCursor(currentCursor_);
    }

    // --- Resize path ---
    if (target && targetIsResize && target->handleResizeInput(e, mouseX, mouseY, isShiftPressed)) {
        if (!undoCaptured_ && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            undoBeforeX_ = target->x;
            undoBeforeY_ = target->y;
            undoBeforeW_ = target->w;
            undoBeforeH_ = target->h;
            undoIsResize_ = !isShiftPressed;
            undoCaptured_ = true;
        }
        if (undoCaptured_ && e.type == SDL_EVENT_MOUSE_BUTTON_UP && prevCapture) {
            float afterX = prevCapture->x, afterY = prevCapture->y;
            float afterW = prevCapture->w, afterH = prevCapture->h;
            int ewid = prevCapture->id;
            std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
            Project* proj = nm ? nm->project : nullptr;
            if (proj && proj->um && prevCapture->trackMoveResizeInUndo()) {
                if (undoIsResize_) {
                    if (undoBeforeX_ != afterX || undoBeforeY_ != afterY ||
                        undoBeforeW_ != afterW || undoBeforeH_ != afterH) {
                        auto* action = new ResizeEmbeddedWindowAction(proj, mgrPath, ewid,
                            undoBeforeX_, undoBeforeY_, undoBeforeW_, undoBeforeH_,
                            afterX, afterY, afterW, afterH);
                        proj->um->newAction(action);
                    }
                } else {
                    if (undoBeforeX_ != afterX || undoBeforeY_ != afterY) {
                        auto* action = new MoveEmbeddedWindowAction(proj, mgrPath, ewid,
                            undoBeforeX_, undoBeforeY_, afterX, afterY);
                        proj->um->newAction(action);
                    }
                }
            }
            undoCaptured_ = false;
        }
        if (releaseCaptureAfter) {
            capturedEmbeddedWindow_->captured_ = false;
            capturedEmbeddedWindow_ = nullptr;
            captureKind_ = CaptureKind::None;
        }
        return true;
    }

    // --- Content / connection path ---
    if (target && !targetIsResize && target->EmbeddedWindow::handleInput(e)) {
        if (releaseCaptureAfter) {
            capturedEmbeddedWindow_->captured_ = false;
            capturedEmbeddedWindow_ = nullptr;
            captureKind_ = CaptureKind::None;
        }
        return true;
    }

    if (releaseCaptureAfter) {
        capturedEmbeddedWindow_->captured_ = false;
        capturedEmbeddedWindow_ = nullptr;
        captureKind_ = CaptureKind::None;
    }

    // --- Cleanup stale pointers ---
    if (capturedEmbeddedWindow_ && !capturedEmbeddedWindow_->visible) {
        capturedEmbeddedWindow_->captured_ = false;
        capturedEmbeddedWindow_ = nullptr;
        captureKind_ = CaptureKind::None;
    }
    if (focusedEmbeddedWindow_ && !focusedEmbeddedWindow_->visible)
        focusedEmbeddedWindow_ = nullptr;

    if (e.type == SDL_EVENT_KEY_DOWN && focusedEmbeddedWindow_) {
        if (!focusedEmbeddedWindow_->visible)
            focusedEmbeddedWindow_ = nullptr;
        else if (focusedEmbeddedWindow_->handleKeyboard(e))
            return true;
    }

    return false;
}

void NodeEditor::render(SDL_Renderer* renderer, SDL_FRect* surfaceRect) {
    if (topMargin > 0.f)
        renderRootMenuBarSkeleton(renderer, surfaceRect);

    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
    SDL_RenderFillRect(renderer, &nodeRect);

    SDL_Rect clip{
        static_cast<int>(std::floor(nodeRect.x)),
        static_cast<int>(std::floor(nodeRect.y)),
        static_cast<int>(std::ceil(nodeRect.w)),
        static_cast<int>(std::ceil(nodeRect.h)),
    };
    if (clip.w > 0 && clip.h > 0)
        SDL_SetRenderClipRect(renderer, &clip);

    std::vector<Node*> sortedNodes = nm->getNodes();
    sortedNodes.push_back(nm->inNode);
    sortedNodes.push_back(nm->outNode);
    std::sort(sortedNodes.begin(), sortedNodes.end(),
              [](Node* a, Node* b) { return a->zOrder < b->zOrder; });
    for (auto node : sortedNodes) {
        if (!node->visible) continue;
        node->makeConnectionRects();
        node->render(renderer);
    }

    renderConnector(renderer);

    renderEmbeddedWindows(renderer);

    SDL_SetRenderClipRect(renderer, nullptr);
}

json NodeEditor::serializeOpenPianoRolls(const std::vector<int>& managerPath) const {
    json arr = json::array();
    for (auto& ew : embeddedWindows_) {
        auto* pr = dynamic_cast<PianoRoll*>(ew.get());
        if (!pr || !pr->region) continue;
        auto* arrNode = dynamic_cast<ArrangerNode*>(pr->region->parentNode);
        if (!arrNode) continue;
        json j;
        j["managerPath"] = managerPath;
        j["arrangerNodeID"] = static_cast<int>(arrNode->id);
        j["regionID"] = static_cast<int>(pr->region->id);
        j["ewID"] = ew->id;
        j["x"] = ew->x;
        j["y"] = ew->y;
        j["w"] = ew->w;
        j["h"] = ew->h;
        j["zOrder"] = ew->zOrder;
        arr.push_back(j);
    }
    return arr;
}

void NodeEditor::restoreOpenPianoRolls(const json& arr) {
    for (auto& j : arr) {
        const auto& mgrPath = j["managerPath"].get<std::vector<int>>();
        int arrangerNodeID = j["arrangerNodeID"].get<int>();
        int regionID = j["regionID"].get<int>();
        if (!nm || nm->managerPath != mgrPath) continue; // skip if not this manager
        auto* node = nm->getNode(static_cast<uint16_t>(arrangerNodeID));
        auto* arrNode = dynamic_cast<ArrangerNode*>(node);
        if (!arrNode) continue;
        arrNode->ensureSongRoll();
        if (!arrNode->sl) continue;
        auto* region = dynamic_cast<Region*>(arrNode->elements->getElement(static_cast<uint16_t>(regionID)));
        if (!region) continue;
        arrNode->sl->createPianoRoll(region, false, j.value("ewID", -1));
        if (arrNode->sl->pianoRolls.empty()) continue;
        auto* pr = arrNode->sl->pianoRolls.back();
        pr->applyGeometry(j["x"], j["y"], j["w"], j["h"]);
        pr->zOrder = j.value("zOrder", pr->zOrder);
    }
}
