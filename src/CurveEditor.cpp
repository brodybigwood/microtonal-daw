#include "CurveEditor.h"
#include "AutomationCurve.h"
#include "GridView.h"
#include "Note.h"
#include "PianoRollInternal.h"  // addVec, subVec
#include "TreeEntry.h"
#include <algorithm>
#include <cmath>

static void fillCircle(SDL_Renderer* r, float cx, float cy, float radius) {
    for (float dy = -radius; dy <= radius; dy += 1.f) {
        float dx = std::sqrt(radius * radius - dy * dy);
        SDL_RenderLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void drawCircle(SDL_Renderer* r, float cx, float cy, float radius) {
    const int segs = 16;
    for (int i = 0; i < segs; ++i) {
        float a1 = static_cast<float>(i) * 2.f * static_cast<float>(M_PI) / static_cast<float>(segs);
        float a2 = static_cast<float>(i + 1) * 2.f * static_cast<float>(M_PI) / static_cast<float>(segs);
        SDL_RenderLine(r, cx + std::cos(a1) * radius, cy + std::sin(a1) * radius,
                         cx + std::cos(a2) * radius, cy + std::sin(a2) * radius);
    }
}

CurveEditor::CurveEditor(
    AutomationCurve* curve,
    GridView* gv,
    const std::vector<std::pair<int,int>>* startVec,
    const std::vector<std::pair<int,int>>* endVec,
    const std::vector<std::pair<int,int>>* offsetVec
) : curve_(curve), gridView_(gv), startVec_(startVec), endVec_(endVec), offsetVec_(offsetVec) {
}

float CurveEditor::startSec() const {
    return Note::beatsFromVector(*startVec_);
}

float CurveEditor::endSec() const {
    return Note::beatsFromVector(*endVec_);
}

float CurveEditor::contentOriginSec() const {
    float off = offsetVec_ ? Note::beatsFromVector(*offsetVec_) : 0.f;
    return startSec() - off;
}

float CurveEditor::tx(const std::vector<std::pair<int,int>>& relVec) const {
    return gridView_->getX(contentOriginSec() + Note::beatsFromVector(relVec));
}

void CurveEditor::updateArea(float y, float h) {
    float e = endSec();
    area = {gridView_->getX(startSec()), y, gridView_->getX(e) - gridView_->getX(startSec()), h};
}

float CurveEditor::ty(float v) const {
    float range = curve_->valueRangeMax - curve_->valueRangeMin;
    float norm = (range > 0.f) ? (v - curve_->valueRangeMin) / range : 0.f;
    return area.y + area.h - norm * area.h;
}

void CurveEditor::render(SDL_Renderer* renderer) {
    if (area.w <= 0 || area.h <= 0) return;

    for (const auto& rl : gridView_->rhythmLines) {
        float x = gridView_->getX(rl.seconds);
        if (x < area.x || x > area.x + area.w) continue;
        if (rl.isBeat) {
            SDL_SetRenderDrawColor(renderer, 100, 100, 120, 100);
            SDL_RenderLine(renderer, x, area.y, x, area.y + area.h);
        } else {
            SDL_SetRenderDrawColor(renderer, 60, 60, 70, 60);
            SDL_RenderLine(renderer, x, area.y, x, area.y + area.h);
        }
    }

    if (curve_->points.size() >= 2) {
        SDL_SetRenderDrawColor(renderer, 255, 200, 100, 255);
        const float s = startSec();
        const float e = endSec();
        const float stepSec = (endSec() - startSec()) / area.w;
        float prevX = 0.f, prevY = 0.f;
        bool first = true;
        for (float sec = s; sec <= e + stepSec * 0.5f; sec += stepSec) {
            float v = curve_->evaluateAtSec(sec, contentOriginSec(), e);
            float px = gridView_->getX(sec);
            float py = ty(v);
            if (!first)
                SDL_RenderLine(renderer, prevX, prevY, px, py);
            prevX = px;
            prevY = py;
            first = false;
        }
    }

    for (size_t i = 0; i < curve_->points.size(); ++i) {
        const auto& pt = curve_->points[i];
        float cx = tx(pt.timeVec);
        float cy = ty(pt.v);
        if (cx < area.x - kHandleRadius || cx > area.x + area.w + kHandleRadius) continue;

        bool hovered = static_cast<int>(i) == hoveredPointIdx;
        SDL_SetRenderDrawColor(renderer, hovered ? 255 : 200, hovered ? 255 : 160, 60, 255);
        fillCircle(renderer, cx, cy, kHandleRadius);
        SDL_SetRenderDrawColor(renderer, 40, 40, 30, 255);
        drawCircle(renderer, cx, cy, kHandleRadius);
    }
    // Tension handles for Smooth segments
    for (size_t i = 0; i + 1 < curve_->points.size(); ++i) {
        if (curve_->points[i].shape.type == CurveShape::Hold) continue;
        float hx, hy;
        tensionHandlePos(i, hx, hy);
        bool hovered = static_cast<int>(i) == hoveredTensionSeg;
        SDL_SetRenderDrawColor(renderer, hovered ? 255 : 180, hovered ? 200 : 120, 60, 255);
        fillCircle(renderer, hx, hy, 4.f);
        SDL_SetRenderDrawColor(renderer, 40, 40, 30, 255);
        drawCircle(renderer, hx, hy, 4.f);
    }
}

int CurveEditor::hitTestPoint(float mx, float my) {
    for (size_t i = 0; i < curve_->points.size(); ++i) {
        float cx = tx(curve_->points[i].timeVec);
        float cy = ty(curve_->points[i].v);
        float dx = mx - cx;
        float dy = my - cy;
        if (dx * dx + dy * dy <= kHandleRadius * kHandleRadius * 4.f)
            return static_cast<int>(i);
    }
    return -1;
}

void CurveEditor::tensionHandlePos(size_t seg, float& outX, float& outY) {
    const auto& a = curve_->points[seg];
    const auto& b = curve_->points[seg + 1];
    float midSec = (Note::beatsFromVector(a.timeVec) + Note::beatsFromVector(b.timeVec)) * 0.5f;
    float absSec = contentOriginSec() + midSec;
    outX = gridView_->getX(absSec);
    outY = ty(curve_->evaluateAtSec(absSec, contentOriginSec(), endSec()));
}

int CurveEditor::hitTestTension(float mx, float my) {
    for (size_t i = 0; i + 1 < curve_->points.size(); ++i) {
        if (curve_->points[i].shape.type == CurveShape::Hold) continue;
        float hx, hy;
        tensionHandlePos(i, hx, hy);
        float dx = mx - hx, dy = my - hy;
        if (dx * dx + dy * dy <= kHandleRadius * kHandleRadius)
            return static_cast<int>(i);
    }
    return -1;
}

float CurveEditor::hitTestCurve(float mx, float my) {
    return -1.f;
}

bool CurveEditor::clampBeatToNeighbors(int pointIndex, float beat,
                                        const std::vector<std::pair<int,int>>** outNeighborVec) const {
    if (pointIndex > 0) {
        float prevBeat = Note::beatsFromVector(curve_->points[pointIndex - 1].timeVec);
        if (beat <= prevBeat) {
            *outNeighborVec = &curve_->points[pointIndex - 1].timeVec;
            return true;
        }
    }
    if (pointIndex < static_cast<int>(curve_->points.size()) - 1) {
        float nextBeat = Note::beatsFromVector(curve_->points[pointIndex + 1].timeVec);
        if (beat >= nextBeat) {
            *outNeighborVec = &curve_->points[pointIndex + 1].timeVec;
            return true;
        }
    }
    return false;
}

std::shared_ptr<TreeEntry> CurveEditor::buildPointMenu(int pointIndex,
                                                        std::function<void()> onWillModify,
                                                        std::function<void()> onDidModify) {
    auto root = std::make_shared<TreeEntry>("Point " + std::to_string(pointIndex));

    const char* names[] = {"Hold", "Single", "Double"};
    for (int s = 0; s < 3; ++s) {
        auto e = std::make_shared<TreeEntry>(names[s]);
        auto st = static_cast<CurveShape::Type>(s);
        e->click = [this, pointIndex, st, onWillModify, onDidModify]() {
            if (static_cast<size_t>(pointIndex) < curve_->points.size()) {
                onWillModify();
                curve_->points[pointIndex].shape.type = st;
                curve_->points[pointIndex].shape.param = 0.f;
                onDidModify();
            }
        };
        root->addChild(e);
    }

    auto delEntry = std::make_shared<TreeEntry>("Delete");
    delEntry->click = [this, pointIndex, onWillModify, onDidModify]() {
        onWillModify();
        curve_->removePoint(static_cast<size_t>(pointIndex));
        onDidModify();
    };
    root->addChild(delEntry);

    return root;
}
