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
        if (movingNode) {
            movingNode->moving = false;
            movingNode = nullptr;
        }
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
    if (!embedded_) {
        // Right-click doesn't give focus, so SDL_GetMouseState returns coords
        // relative to the wrong window. Use global state and convert.
        float gx, gy;
        SDL_GetGlobalMouseState(&gx, &gy);
        int wx = 0, wy = 0;
        if (nm && nm->project && nm->project->window)
            SDL_GetWindowPosition(nm->project->window, &wx, &wy);
        mouseX = gx - static_cast<float>(wx);
        mouseY = gy - static_cast<float>(wy);
    }

    if (movingNode) movingNode->move(movingNodeStartX + mouseX - moveOffX, movingNodeStartY + mouseY - moveOffY);
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
        SDL_Window* evWin = SDL_GetWindowFromID(e.button.windowID);
        SDL_Renderer* evRenderer = evWin ? SDL_GetRenderer(evWin) : nullptr;
        ctxMenu->activate(evRenderer ? evRenderer : (nm && nm->project ? nm->project->renderer : nullptr), e.button.windowID);

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

