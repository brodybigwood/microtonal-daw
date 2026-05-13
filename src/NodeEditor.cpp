#include "NodeEditor.h"
#include "NodeManager.h"
#include "Node.h"
#include "ContextMenu.h"
#include "WindowHandler.h"
#include "UndoManager.h"
#include <iostream>
#include "Preferences.h"
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
        if (n->detached) n->detach();
        else n->attach();
        n->clearTextures();
    }
    if (nm->inNode->detached) nm->inNode->detach();
    else nm->inNode->attach();
    nm->inNode->clearTextures();
    if (nm->outNode->detached) nm->outNode->detach();
    else nm->outNode->attach();
    nm->outNode->clearTextures();
}

void NodeEditor::setMovingNode(Node* node) {
    releaseMovingNode();
    movingNode = node;
    node->moving = true;
    movingNodeStartX = node->dstRect.x;
    movingNodeStartY = node->dstRect.y;
    moveOffX = mouseX - node->dstRect.x;
    moveOffY = mouseY - node->dstRect.y;
}

void NodeEditor::releaseMovingNode(bool commitAction) {
    if (!movingNode) return;
    float endX = movingNode->dstRect.x;
    float endY = movingNode->dstRect.y;
    int movingID = movingNode->id;
    movingNode->moving = false;
    movingNode = nullptr;
    if (commitAction && (endX != movingNodeStartX || endY != movingNodeStartY)) {
        auto pa = new MoveNodeAction(nm->project, nm->managerPath, movingID, movingNodeStartX, movingNodeStartY, endX, endY);
        nm->project->um->newAction(pa);
    }
}

void NodeEditor::cancelMovingNode() {
    if (!movingNode) return;
    movingNode->move(movingNodeStartX, movingNodeStartY);
    releaseMovingNode(false);
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
}

NodeEditor::NodeEditor() :
    isAltPressed(WindowHandler::instance()->isAltPressed),
    isCtrlPressed(WindowHandler::instance()->isCtrlPressed) {
}

NodeEditor::~NodeEditor() {
    renderer = nullptr;
    window = nullptr;
}

void NodeEditor::setEmbeddedCanvasSize(float w, float h) {
    canvasW_ = w;
    canvasH_ = h;
    nodeRect = SDL_FRect{0.f, 0.f, w, h};
}

void NodeEditor::resetRootMenuBarLayout() {
    topMargin = 0.f;
    nodeRect = SDL_FRect{0.f, 0.f, canvasW_, canvasH_};
    portModeButtonRect.x = 12.f;
    portModeButtonRect.y = 12.f;
}

void NodeEditor::updateRootMenuBarLayout() {
    resetRootMenuBarLayout();
    if (!nm || !menuBarHostNode_) return;
    /* Top-level patcher: the patcher node lives on the processor canvas (`nm` has empty `managerPath`). */
    if (!menuBarHostNode_->nm || !menuBarHostNode_->nm->managerPath.empty()) return;
    topMargin = kRootMenuBarStripH;
    nodeRect = SDL_FRect{0.f, topMargin, canvasW_, std::max(1.f, canvasH_ - topMargin)};
    portModeButtonRect.y = 12.f + topMargin;
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

    for (const char* label : kLabels) {
        int tw = 0;
        int th = 0;
        SDL_Surface* surf = nullptr;
        if (fonts.mainFont)
            surf = TTF_RenderText_Blended(fonts.mainFont, label, 0, SDL_Color{255, 255, 255, 255});
        if (surf) {
            tw = surf->w;
            th = surf->h;
        }
        const float w = std::max(kMinItemW, static_cast<float>(tw) + 16.f);
        const SDL_FRect cell{x, y, w, h};
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

void NodeEditor::renderConnector(SDL_Renderer* renderer) {

    int x;
    int y;
    Connection* conn;

    if (dstNode != nullptr) {
        if (dstNodeID != -1) {
            conn = dstNode->inputs.getConnection(static_cast<uint16_t>(dstNodeID));
            if (!conn) return;
            auto rect = conn->rect;
            x = rect.x + rect.w/2.0f;
            y = rect.y + rect.h/2.0f;
        } else return;
    } else if (srcNode != nullptr) {
        if (srcNodeID != -1) {
            conn = srcNode->outputs.getConnection(static_cast<uint16_t>(srcNodeID));
            if (!conn) return;
            auto rect = conn->rect;
            x = rect.x + rect.w/2.0f;
            y = rect.y + rect.h/2.0f;
        } else return;
    } else return;

    SDL_FColor color;
    if (conn->type == DataType::Events) color = {0.5f, 1.0f, 0.5f, 1.0f}; 
    else color = {1.0f, 0.5f, 0.5f, 1.0f};
    renderSine(mouseX, mouseY, x, y, color);
}

void NodeEditor::renderSine(float x1, float y1, float x2, float y2, SDL_FColor color) {
    renderPatchCable(renderer, x1, y1, x2, y2, color);
}

void NodeEditor::tick() {
    if (!nm || !renderer) return;
    SDL_FRect surface{0.f, 0.f, canvasW_, canvasH_};
    render(renderer, &surface);
}

void NodeEditor::togglePortDisplayMode() {
    portDisplayMode = (portDisplayMode == PortDisplayMode::RectLabels)
        ? PortDisplayMode::SquareIDs
        : PortDisplayMode::RectLabels;
    if (nm) nm->portDisplayMode = portDisplayMode;

    for (auto* n : nm->getNodes()) n->makeConnectionRects();
    nm->inNode->makeConnectionRects();
    nm->outNode->makeConnectionRects();
}

void NodeEditor::renderPresent() {
    for (auto n : nm->getNodes()) {
        n->renderPresent();
    }
    nm->inNode->renderPresent();
    SDL_RenderPresent(renderer);
}

uint32_t NodeEditor::getWindowID() {
    return SDL_GetWindowID(window);
}

void NodeEditor::move() {
    auto x = mouseX - moveOffX;
    auto y = mouseY - moveOffY;

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
    moveMouse();

    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        releaseMovingNode();
        leftClick = false;
    }

    if (topMargin > 0.f && isPointerOverMenuBar(mouseX, mouseY)) {
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

    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (isCtrlPressed && leftClick) move();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            leftClick = true;
            moveOffX = mouseX;
            moveOffY = mouseY;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (isCtrlPressed) zoom(std::pow(1.1, e.wheel.y));
            break;
        default:
            break;
    }
    if (nm->inNode->handleInput(e)) return;
    if (nm->outNode->handleInput(e)) return;
    for (auto n : nm->getNodes()) {
        if (n->handleInput(e)) return;
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
        if (inside(mouseX, mouseY, &portModeButtonRect)) {
            togglePortDisplayMode();
            return;
        }

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
        ctxMenu->active = true;

        ctxMenu->window_id = SDL_GetWindowID(window);
        ctxMenu->renderer = renderer;

        ctxMenu->locX = mouseX;
        ctxMenu->locY = mouseY;

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

    SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
    SDL_RenderFillRect(renderer, &portModeButtonRect);
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderRect(renderer, &portModeButtonRect);

    if (fonts.mainFont) {
        const char* modeText = (portDisplayMode == PortDisplayMode::RectLabels)
            ? "Ports: Rect Labels"
            : "Ports: Square IDs";
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, modeText, 0, SDL_Color{0, 0, 0, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                const float scale = std::min(1.0f, (portModeButtonRect.w - 12.0f) / static_cast<float>(surf->w));
                const float tw = static_cast<float>(surf->w) * scale;
                const float th = static_cast<float>(surf->h) * scale;
                SDL_FRect tr{
                    portModeButtonRect.x + (portModeButtonRect.w - tw) * 0.5f,
                    portModeButtonRect.y + (portModeButtonRect.h - th) * 0.5f,
                    tw, th
                };
                SDL_RenderTexture(renderer, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }

    for (auto node : nm->getNodes()) {
        node->render();
    }

    nm->inNode->render();
    nm->outNode->render();

    renderConnector(this->renderer);

    SDL_SetRenderClipRect(renderer, nullptr);
}
