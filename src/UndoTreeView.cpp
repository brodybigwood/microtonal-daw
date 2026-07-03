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

// Undo-tree window rendering, layout and hit-testing (split from UndoManager.cpp).

bool UndoManager::mouseHitsRect(SDL_FRect* rect) const {
    if (!hitTestWindow)
        return MouseOn(rect);
    float gx = 0.0f;
    float gy = 0.0f;
    SDL_GetGlobalMouseState(&gx, &gy);
    int wx = 0;
    int wy = 0;
    SDL_GetWindowPosition(hitTestWindow, &wx, &wy);
    const float lx = gx - static_cast<float>(wx);
    const float ly = gy - static_cast<float>(wy);
    return lx >= rect->x && lx < rect->x + rect->w && ly >= rect->y && ly < rect->y + rect->h;
}

void UndoManager::clearAllRenderTextures() {
    const std::function<void(ProjectAction*)> wipe = [&](ProjectAction* pa) {
        if (pa->texture) {
            SDL_DestroyTexture(pa->texture);
            pa->texture = nullptr;
        }
        for (ProjectAction* c : pa->children)
            wipe(c);
    };
    if (head)
        wipe(head);
}

namespace {

ProjectAction* directChildOnPath(ProjectAction* root, ProjectAction* descendant) {
    if (!root || !descendant || descendant == root)
        return nullptr;
    for (ProjectAction* n = descendant; n; n = n->parent) {
        if (n->parent == root)
            return n;
        if (n == root)
            return nullptr;
    }
    return nullptr;
}

struct UndoTreeProbe {
    ProjectAction* hit = nullptr;
    float bottomY = 0.f;
};

UndoTreeProbe probeUndoTreeAt(float lx, float ly, float x, float y, float layoutFullWidth, float rowH, ProjectAction* pa) {
    SDL_FRect row{x, y, layoutFullWidth, rowH};
    ProjectAction* selfHit = nullptr;
    if (lx >= row.x && lx < row.x + row.w && ly >= row.y && ly < row.y + row.h)
        selfHit = pa;
    float cursorY = y + rowH;
    if (!pa->undoTreeExpanded || pa->children.empty())
        return {selfHit, cursorY};
    const float childLeft = x + rowH;
    ProjectAction* best = selfHit;
    for (ProjectAction* c : pa->children) {
        UndoTreeProbe sub = probeUndoTreeAt(lx, ly, childLeft, cursorY, layoutFullWidth, rowH, c);
        if (sub.hit)
            best = sub.hit;
        cursorY = sub.bottomY;
    }
    return {best, cursorY};
}

bool undoNodeChainReachHead(ProjectAction* n, ProjectAction* realHead) {
    while (n) {
        if (n == realHead)
            return true;
        n = n->parent;
    }
    return false;
}

void sanitizeUndoTreeViewRoot(UndoManager& um) {
    if (!um.head) {
        um.undoTreeViewRoot = nullptr;
        return;
    }
    if (um.undoTreeViewRoot && !undoNodeChainReachHead(um.undoTreeViewRoot, um.head))
        um.undoTreeViewRoot = nullptr;
}

} // namespace

void UndoManager::undoTreeHandleWheel(const SDL_FRect& layoutAnchor, float rowPixels, float mouseX, float mouseY,
    float wheelY) {
    if (!head)
        return;
    sanitizeUndoTreeViewRoot(*this);
    const float rowH = rowPixels > 0.f ? rowPixels : undoTreeRowH;
    ProjectAction* vr = undoTreeViewRoot ? undoTreeViewRoot : head;

    constexpr float eps = 0.05f;
    if (std::fabs(wheelY) < eps)
        return;

    /* SDL: positive Y scrolls finger away — treat as drill down into a child row. */
    if (wheelY < 0.f) {
        if (vr != head && vr->parent)
            undoTreeViewRoot = (vr->parent == head) ? nullptr : vr->parent;
        return;
    }

    ProjectAction* h = probeUndoTreeAt(mouseX, mouseY, layoutAnchor.x, layoutAnchor.y, layoutAnchor.w, rowH, vr).hit;

    ProjectAction* const cChild = (current != vr) ? directChildOnPath(vr, current) : nullptr;
    ProjectAction* const hChild = (h && h != vr) ? directChildOnPath(vr, h) : nullptr;

    /* Prefer the branch under the hovered row; otherwise follow the path to the undo tip. */
    ProjectAction* next = hChild ? hChild : cChild;

    if (!next && !vr->children.empty()) {
        int idx = vr->last_index;
        const int nch = static_cast<int>(vr->children.size());
        if (idx < 0 || idx >= nch)
            idx = nch - 1;
        next = vr->children[static_cast<size_t>(idx)];
    }

    if (next)
        undoTreeViewRoot = next;
}

void UndoManager::syncUndoTreeExpansionPathToCurrent() {
    if (!head || !current)
        return;
    for (ProjectAction* n = current; n && n != head; n = n->parent) {
        if (n->parent)
            n->parent->undoTreeExpanded = true;
    }
}

void UndoManager::applyUndoTreeClickAfterLayout() {
    const bool left = undoTreeFrameLeft;
    const bool right = undoTreeFrameRight;
    undoTreeFrameLeft = false;
    undoTreeFrameRight = false;
    ProjectAction* hit = undoTreeHitUnderCursor;
    undoTreeHitUnderCursor = nullptr;
    if (!hit)
        return;
    /* Prefer navigation if both landed same frame (e.g. odd hardware). */
    if (left) {
        goTo(hit);
        return;
    }
    if (right && !hit->children.empty())
        hit->undoTreeExpanded = !hit->undoTreeExpanded;
}

bool UndoManager::drawUndoTreeRow(SDL_Renderer* renderer, SDL_FRect* rect, ProjectAction* pa) {
    bool hovering = false;

    SDL_Color color;
    if (pa == current) {
        if (mouseHitsRect(rect))
            color = {100, 200, 100, 255};
        else
            color = {100, 255, 100, 255};
    } else {
        if (mouseHitsRect(rect))
            color = {255, 255, 255, 255};
        else
            color = {200, 200, 200, 255};
    }

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, rect);

    SDL_Texture*& texture = pa->texture;
    if (!texture) {
        SDL_Color textColor{0, 0, 0, 255};
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, pa->name.c_str(), 0, textColor);
        texture = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);
    }

    SDL_RenderTexture(renderer, texture, nullptr, rect);

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderRect(renderer, rect);

    if (mouseHitsRect(rect)) {
        hovering = true;
        if (undoTreeFrameLeft || undoTreeFrameRight)
            undoTreeHitUnderCursor = pa;
    }

    return hovering;
}

UndoManager::UndoTreeLayoutBox UndoManager::layoutUndoTreeGeom(SDL_Renderer* renderer, float x, float y, float w, float rowH, ProjectAction* pa) {
    UndoTreeLayoutBox out;
    SDL_FRect row{x, y, w, rowH};
    out.hovering = drawUndoTreeRow(renderer, &row, pa);
    float cursorY = y + rowH;

    if (!pa->undoTreeExpanded || pa->children.empty()) {
        out.bottomY = cursorY;
        return out;
    }

    const float kIndent = rowH;
    const float childLeft = x + kIndent;
    const float childW = baseRect->w;
    const float trunkX = row.x + kIndent * 0.5f;

    std::vector<float> directChildRowMidY;
    directChildRowMidY.reserve(pa->children.size());
    for (ProjectAction* c : pa->children) {
        const float sliceTop = cursorY;
        directChildRowMidY.push_back(sliceTop + rowH * 0.5f);

        UndoTreeLayoutBox sub =
            layoutUndoTreeGeom(renderer, childLeft, cursorY, childW, rowH, c);
        out.hovering |= sub.hovering;
        cursorY = sub.bottomY;
    }

    /* Vertical spine to the last sibling’s row mid; one horizontal per direct child at its own row.
       Expanding a child only adds pixels below that row — earlier horizontals stay put. */
    const float trunkTop = row.y + row.h;
    const float spineBottom = directChildRowMidY.back();
    const float hRunEnd = std::fmax(childLeft - 2.5f, trunkX);

    SDL_SetRenderDrawColor(renderer, 160, 160, 164, 255);
    SDL_RenderLine(renderer, trunkX, trunkTop + 0.5f, trunkX, spineBottom + 0.5f);
    for (float midY : directChildRowMidY)
        SDL_RenderLine(renderer, trunkX, midY + 0.5f, hRunEnd, midY + 0.5f);

    out.bottomY = cursorY;
    return out;
}

bool UndoManager::render(SDL_Renderer* renderer) {
    if (!baseRect || !head)
        return false;
    sanitizeUndoTreeViewRoot(*this);
    syncUndoTreeExpansionPathToCurrent();
    undoTreeHitUnderCursor = nullptr;
    undoTreeFrameLeft = undoTreePendingLeft;
    undoTreeFrameRight = undoTreePendingRight;
    undoTreePendingLeft = false;
    undoTreePendingRight = false;
    const float rowH = undoTreeRowH > 1.f ? undoTreeRowH : 20.f;
    auto viewRoot = [&]() -> ProjectAction* { return undoTreeViewRoot ? undoTreeViewRoot : head; };
    UndoTreeLayoutBox box =
        layoutUndoTreeGeom(renderer, baseRect->x, baseRect->y, baseRect->w, rowH, viewRoot());
    ProjectAction* const tipBeforeClick = current;
    applyUndoTreeClickAfterLayout();
    syncUndoTreeExpansionPathToCurrent();
    sanitizeUndoTreeViewRoot(*this);
    if (current != tipBeforeClick)
        box = layoutUndoTreeGeom(renderer, baseRect->x, baseRect->y, baseRect->w, rowH, viewRoot());
    return box.hovering;
}

