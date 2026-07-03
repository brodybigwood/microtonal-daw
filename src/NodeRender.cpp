#include "Node.h"
#include "NodeManager.h"
#include "SDL_Events.h"
#include "ContextMenu.h"
#include <iostream>
#include "NodeEditor.h"
#include "WindowHandler.h"
#include "Preferences.h"
#include "Settings.h"
#include "UndoManager.h"
#include <cstring>
#include <limits>
#include <sstream>
#include <iomanip>
#include "styles.h"

// Node and Connection rendering (split from Node.cpp).

SDL_FRect Connection::srcRect() {
    if (!nm) return SDL_FRect{0, 0, 0, 0};
    Node* n = nm->getNode(input_node);
    if (!n) return SDL_FRect{0, 0, 0, 0};
    connectionSet& outputs = n->outputs;
    auto conn = outputs.getConnection(input_connection);
    if (!conn) return SDL_FRect{0, 0, 0, 0};
    return conn->rect;
}

void Node::renderContent(SDL_Renderer* renderer) {
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];

        vx[0] = 0;       vx[1] = NODE_W;    vx[2] = NODE_W - 40; vx[3] = 40;
        vy[0] = 0;       vy[1] = 0;         vy[2] = NODE_H;      vy[3] = NODE_H;
    }

    filledPolygonRGBA(renderer, vx, vy, vCount, 255, 255, 255, 255);
    aapolygonRGBA(renderer, vx, vy, vCount, 0, 0, 0, 255);

    renderParams(renderer);
}

void Node::renderContentHelper(SDL_Renderer* renderer) {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    renderContent(renderer);
    SDL_SetRenderTarget(renderer, target);
    // Texture is 1:1 with dstRect, just blit at the node's position.
    SDL_FRect src{0, 0, dstRect.w, dstRect.h};
    SDL_RenderTexture(renderer, texture, &src, &dstRect);
}

void Node::render(SDL_Renderer* renderer) {
    if (!renderer)
        return;

    if (!texture)
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, NODE_W, NODE_H);

    renderContentHelper(renderer);

    // Polygon outline, ports, and tooltip all render onto the same renderer.
    SDL_Renderer* portR = renderer;

    // Polygon outline on the canvas, offset by node position.
    if (vCount >= 3 && vx && vy) {
        std::vector<float> svx(vCount);
        std::vector<float> svy(vCount);
        for (size_t i = 0; i < vCount; ++i) {
            svx[i] = dstRect.x + vx[i];
            svy[i] = dstRect.y + vy[i];
        }

        // Glow pass — expand outward, low alpha.
        float cx = 0.f, cy = 0.f;
        for (size_t i = 0; i < vCount; ++i) { cx += svx[i]; cy += svy[i]; }
        cx /= static_cast<float>(vCount);
        cy /= static_cast<float>(vCount);
        constexpr float kGlow = 3.f;
        std::vector<float> gvx(vCount);
        std::vector<float> gvy(vCount);
        for (size_t i = 0; i < vCount; ++i) {
            float dx = svx[i] - cx;
            float dy = svy[i] - cy;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.001f) {
                gvx[i] = svx[i] + dx / len * kGlow;
                gvy[i] = svy[i] + dy / len * kGlow;
            } else {
                gvx[i] = svx[i];
                gvy[i] = svy[i];
            }
        }
        aapolygonRGBA(portR, gvx.data(), gvy.data(), static_cast<int>(vCount), 0, 0, 0, 60);

        // Crisp outline at exact polygon edge.
        aapolygonRGBA(portR, svx.data(), svy.data(), static_cast<int>(vCount), 0, 0, 0, 220);
    }

    if (showConnectionPorts()) {
        for (auto* conn : inputs.connections) {
            if (conn) conn->render(portR, conn->id == hoveredConnection && hoveredDirection == Direction::input);
        }
        for (auto* conn : outputs.connections) {
            if (conn) conn->render(portR, conn->id == hoveredConnection && hoveredDirection == Direction::output);
        }
    }

    // Tooltip after all ports/cables so it draws on top
    if (hoveredConnection != -1 && fonts.mainFont) {
        Connection* hovered = nullptr;
        for (auto* conn : inputs.connections) {
            if (conn && conn->id == hoveredConnection && hoveredDirection == Direction::input) { hovered = conn; break; }
        }
        if (!hovered) {
            for (auto* conn : outputs.connections) {
                if (conn && conn->id == hoveredConnection && hoveredDirection == Direction::output) { hovered = conn; break; }
            }
        }
        if (hovered && ne) {
            if (ne->mouseX < hovered->rect.x || ne->mouseX > hovered->rect.x + hovered->rect.w ||
                ne->mouseY < hovered->rect.y || ne->mouseY > hovered->rect.y + hovered->rect.h)
                hovered = nullptr;
        }
        if (hovered) {
            const std::string tipText = hovered->label.empty()
                ? (std::string(hovered->dir == Direction::input ? "Input " : "Output ") + std::to_string(hovered->id))
                : hovered->label;
            const float mx = ne ? ne->mouseX : 0.f;
            const float my = ne ? ne->mouseY : 0.f;
            SDL_Surface* tipSurf = TTF_RenderText_Blended(fonts.mainFont, tipText.c_str(), 0, SDL_Color{255, 255, 255, 255});
            if (tipSurf) {
                SDL_Texture* tipTex = SDL_CreateTextureFromSurface(portR, tipSurf);
                if (tipTex) {
                    const float pad = 4.0f;
                    SDL_FRect bg{mx + 12.0f, my + 12.0f, static_cast<float>(tipSurf->w) + pad * 2, static_cast<float>(tipSurf->h) + pad * 2};
                    SDL_SetRenderDrawColor(portR, 30, 30, 30, 230);
                    SDL_RenderFillRect(portR, &bg);
                    SDL_SetRenderDrawColor(portR, 180, 180, 180, 255);
                    SDL_RenderRect(portR, &bg);
                    SDL_FRect tr{mx + 12.0f + pad, my + 12.0f + pad, static_cast<float>(tipSurf->w), static_cast<float>(tipSurf->h)};
                    SDL_RenderTexture(portR, tipTex, nullptr, &tr);
                    SDL_DestroyTexture(tipTex);
                }
                SDL_DestroySurface(tipSurf);
            }
        }
    }
}

void Connection::render(SDL_Renderer* renderer, bool hover) {
    if (!renderer) return;

    const PortDisplayMode mode = static_cast<PortDisplayMode>(Settings::instance().portDisplayMode());
    SDL_Color c{128, 128, 128, 255};

    switch (type) {
        case DataType::Events:
            if (is_connected) c = {160, 255, 160, 255};
            else c = {120, 255, 120, 255};
            break;
        case DataType::Waveform:
            if (is_connected) c = {255, 160, 160, 255};
            else c = {255, 120, 120, 255};
            break;
        default:
            break;
    }

    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);

    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 120,120,120,255);
    SDL_RenderRect(renderer, &rect);

    if (fonts.mainFont) {
        std::string text;
        if (mode == PortDisplayMode::SquareIDs) {
            text = std::to_string(id);
        } else {
            const std::string fallback = std::string(dir == Direction::input ? "Input " : "Output ") + std::to_string(id);
            text = label.empty() ? fallback : label;
            if (text.size() > 8) {
                text = text.substr(0, 8);
            }
        }
        if (!text.empty()) {
            SDL_Color textColor{10, 10, 10, 255};
            SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text.c_str(), 0, textColor);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    const float maxW = rect.w * 0.86f;
                    const float maxH = rect.h * 0.86f;
                    const float baseScale = std::min(
                        1.0f,
                        std::min(maxW / static_cast<float>(surf->w),
                                 maxH / static_cast<float>(surf->h))
                    );
                    const float scale = (mode == PortDisplayMode::SquareIDs)
                        ? std::max(0.1f, baseScale)
                        : std::max(0.1f, baseScale * labelScale);
                    const float drawW = static_cast<float>(surf->w) * scale;
                    const float drawH = static_cast<float>(surf->h) * scale;
                    const float cx = rect.x + rect.w * 0.5f;
                    const float cy = rect.y + rect.h * 0.5f;
                    SDL_FRect textRect{cx - drawW * 0.5f, cy - drawH * 0.5f, drawW, drawH};
                    if (mode == PortDisplayMode::RectLabels) {
                        SDL_FPoint center{drawW * 0.5f, drawH * 0.5f};
                        SDL_RenderTextureRotated(renderer, tex, nullptr, &textRect, -90.0, &center, SDL_FLIP_NONE);
                    } else {
                        SDL_RenderTexture(renderer, tex, nullptr, &textRect);
                    }
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }
        }
    }

    if (is_connected && dir == Direction::input) {
        // Suppress normal cable when this connection is being dragged.
        if (nm && nm->ne && nm->ne->isConnectionBeingDragged(this)) return;

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        auto src = srcRect();
        if (src.w > 0.0f || src.h > 0.0f) {
            SDL_FColor color;
            if (type == DataType::Events) color = {0.5f, 1.0f, 0.5f, 1.0f};
            else color = {1.0f, 0.5f, 0.5f, 1.0f};

            NodeEditor::renderPatchCable(renderer, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f,
                src.x + src.w * 0.5f, src.y + src.h * 0.5f, color);
        }
    }

}

void Node::renderParams(SDL_Renderer* renderer) {
    for (auto p : params) p->render(renderer);
}

