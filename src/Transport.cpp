#include "Transport.h"
#include "GridView.h"
#include "Project.h"
#include "PianoRollInternal.h"
#include "AutomationCurve.h"
#include "CurveEditor.h"
#include "UndoManager.h"
#include "ContextMenu.h"
#include "TreeEntry.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

namespace {
constexpr float kBeatLineW = 2.f;

static float snappedBeat(GridView* view, float mouseX) {
    float rawBeat = (mouseX + view->scrollX - view->leftMargin) / view->dW;
    rawBeat = view->adjustTransportSeekSec(rawBeat);
    size_t idx = closestRhythmLineIndexForSeconds(view->rhythmLines, rawBeat);
    if (idx != SIZE_MAX && idx < view->rhythmLines.size())
        return view->rhythmLines[idx].seconds;
    return rawBeat;
}

static json tempoPointsToJson(const std::vector<CurvePoint>& pts) {
    json arr = json::array();
    for (const auto& pt : pts) {
        json jpt;
        jpt["timeVec"] = json::array();
        for (const auto& pr : pt.timeVec)
            jpt["timeVec"].push_back(json::array({pr.first, pr.second}));
        jpt["v"] = pt.v;
        jpt["shape"] = static_cast<int>(pt.shape.type);
        jpt["shapeParam"] = pt.shape.param;
        arr.push_back(jpt);
    }
    return arr;
}

static json g_tempoBefore;
} // namespace

struct Transport::TempoDragState {
    json before;
    int tensionSeg = -1;
    float tensionStartParam = 0.f;
};

Transport::Transport(GridView* view) : view(view), dstRect(view->dstRect) {
    tempoOffsetVec = tempoStartVec;
    tempoCurveEditor = new CurveEditor(
        &view->project->tempoCurve, view,
        &tempoStartVec, &tempoEndVec, &tempoOffsetVec
    );
}

Transport::~Transport() {
    delete tempoCurveEditor;
    delete dragState;
}

void Transport::moveMouse(float mouseX, float mouseY) {
    this->mouseX = mouseX;
    this->mouseY = mouseY;
}

void Transport::updateTempoCurveBounds() {
    if (view->dstRect->w <= 0.f || view->rhythmLines.empty()) return;
    // Screen edges in beat space: getX maps beat→screenX, inverted
    float leftBeat = (view->scrollX - view->leftMargin) / view->dW;
    float rightBeat = (view->scrollX - view->leftMargin + view->dstRect->w) / view->dW;

    // Find rhythm line strictly left of left edge and strictly right of right edge
    size_t li = 0;
    for (size_t i = 0; i < view->rhythmLines.size(); ++i) {
        if (view->rhythmLines[i].seconds < leftBeat) li = i;
        else break;
    }
    size_t ri = view->rhythmLines.size() - 1;
    for (size_t i = view->rhythmLines.size(); i > 0; --i) {
        if (view->rhythmLines[i - 1].seconds > rightBeat) ri = i - 1;
        else break;
    }

    tempoStartVec = view->rhythmLines[li].integerPairs;
    tempoEndVec = view->rhythmLines[ri].integerPairs;
    tempoOffsetVec = tempoStartVec;
}

// -- tempo curve mouse handling (same pattern as SongRoll's curve editing) ----

void Transport::handleTempoMouseDown(float areaH) {
    auto& curve = view->project->tempoCurve;
    auto* ce = tempoCurveEditor;
    delete dragState;
    dragState = new TempoDragState;
    dragState->before = tempoPointsToJson(curve.points);

    if (ce->hoveredPointIdx >= 0) {
        ce->draggingPointIdx = ce->hoveredPointIdx;
        return;
    }

    // Add new point at click position
    float beat = snappedBeat(view, mouseX);

    float normY = 1.f - (mouseY / areaH);
    normY = std::max(0.f, std::min(1.f, normY));
    float bpm = std::max(0.001f, normY * (curve.valueRangeMax - curve.valueRangeMin) + curve.valueRangeMin);

    std::vector<std::pair<int,int>> timeVec;
    size_t idx = closestRhythmLineIndexForSeconds(view->rhythmLines, beat);
    if (idx != SIZE_MAX && idx < view->rhythmLines.size())
        timeVec = view->rhythmLines[idx].integerPairs;

    curve.addPoint(timeVec, bpm);
    for (size_t i = 0; i < curve.points.size(); ++i)
        if (curve.points[i].timeVec == timeVec) { ce->draggingPointIdx = static_cast<int>(i); break; }
}

void Transport::handleTempoMouseMove(float areaH) {
    auto& curve = view->project->tempoCurve;
    auto* ce = tempoCurveEditor;

    if (ce->draggingPointIdx >= 0) {
        int di = ce->draggingPointIdx;
        if (static_cast<size_t>(di) >= curve.points.size()) return;

        float beat = (mouseX + view->scrollX - view->leftMargin) / view->dW;
        beat = view->adjustTransportSeekSec(beat);
        size_t idx = closestRhythmLineIndexForSeconds(view->rhythmLines, beat);
        if (idx == SIZE_MAX || idx >= view->rhythmLines.size()) return;
        const std::vector<std::pair<int,int>>* neighborVec = nullptr;
        if (tempoCurveEditor->clampBeatToNeighbors(di, view->rhythmLines[idx].seconds, &neighborVec))
            curve.points[di].timeVec = *neighborVec;
        else
            curve.points[di].timeVec = view->rhythmLines[idx].integerPairs;

        float normY = 1.f - (mouseY / areaH);
        normY = std::max(0.f, std::min(1.f, normY));
        curve.points[di].v = std::max(0.001f, normY * (curve.valueRangeMax - curve.valueRangeMin) + curve.valueRangeMin);
        return;
    }

    if (ce->hoveredTensionSeg >= 0 && dragState && dragState->tensionSeg < 0) {
        dragState->tensionSeg = ce->hoveredTensionSeg;
        auto& segPt = curve.points[ce->hoveredTensionSeg];
        dragState->tensionStartParam = segPt.shape.param;
    }

    if (dragState && dragState->tensionSeg >= 0) {
        int ts = dragState->tensionSeg;
        if (ts < static_cast<int>(curve.points.size()) - 1) {
            auto& segPt = curve.points[ts];
            float dy = mouseY - (areaH * 0.5f);
            float dir = curve.points[ts + 1].v >= segPt.v ? 1.f : -1.f;
            segPt.shape.param = std::max(-1.f, std::min(1.f,
                dragState->tensionStartParam - dy * dir / (areaH * 0.5f)));
        }
    }
}

// -- public methods ----------------------------------------------------------

void Transport::clickMouse(uint8_t button) {
    const float barY = view->topMargin - kTimelineHeight;

    // Tempo curve area
    if (mouseY >= 0 && mouseY < barY) {
        updateTempoCurveBounds();
        float areaH = view->topMargin - kTimelineHeight;
        tempoCurveEditor->updateArea(view->dstRect->y, areaH);
        tempoCurveEditor->hoveredPointIdx = tempoCurveEditor->hitTestPoint(mouseX, mouseY);
        tempoCurveEditor->hoveredTensionSeg = tempoCurveEditor->hitTestTension(mouseX, mouseY);

        if (button == SDL_BUTTON_RIGHT) {
            int pt = tempoCurveEditor->hoveredPointIdx;
            if (pt < 0 || static_cast<size_t>(pt) >= view->project->tempoCurve.points.size()) return;

            auto ctxMenu = ContextMenu::get();
            ctxMenu->activate();
            auto root = tempoCurveEditor->buildPointMenu(pt,
                [this]() { g_tempoBefore = tempoPointsToJson(view->project->tempoCurve.points); },
                [this]() {
                    view->project->um->newAction(
                        new TempoCurveEditAction(view->project, std::move(g_tempoBefore),
                                                 tempoPointsToJson(view->project->tempoCurve.points)));
                    g_tempoBefore = json::array();
                });
            root->label = "Tempo Point";
            ctxMenu->dynamicTick = getTreeMenuTicker(root);
            return;
        }

        if (button == SDL_BUTTON_LEFT)
            handleTempoMouseDown(areaH);
        return;
    }

    // Timeline bar
    if (mouseY >= barY && mouseY <= barY + Transport::kTimelineHeight) {
        draggingTimeline = true;
        float beat = snappedBeat(view, mouseX);
        view->project->beatPosition = static_cast<double>(beat);
        view->project->effectiveBeatPosition = static_cast<double>(beat);
        double sec = static_cast<double>(view->project->tempoCurve.secondsForBeats(0.f, beat));
        view->project->effectiveTime = sec;
        view->project->timeSeconds = sec;
        view->project->playHeadStart = sec;
        view->project->playHeadBeat = static_cast<double>(beat);
        view->project->syncTransportToAudio(view->project->isPlaying,
                                            static_cast<double>(beat), sec);
    }
}

void Transport::handleMotion() {
    if (draggingTimeline) {
        float beat = snappedBeat(view, mouseX);
        view->project->beatPosition = static_cast<double>(beat);
        view->project->effectiveBeatPosition = static_cast<double>(beat);
        double sec = static_cast<double>(view->project->tempoCurve.secondsForBeats(0.f, beat));
        view->project->effectiveTime = sec;
        view->project->timeSeconds = sec;
        view->project->playHeadStart = sec;
        view->project->playHeadBeat = static_cast<double>(beat);
        view->project->syncTransportToAudio(view->project->isPlaying,
                                            static_cast<double>(beat), sec);
        return;
    }

    // Tempo curve drag
    if (tempoCurveEditor->draggingPointIdx >= 0 || dragState) {
        updateTempoCurveBounds();
        float areaH = view->topMargin - kTimelineHeight;
        tempoCurveEditor->updateArea(view->dstRect->y, areaH);
        handleTempoMouseMove(areaH);
    }
}

void Transport::finishTempoDrag() {
    if (!dragState) return;
    tempoCurveEditor->draggingPointIdx = -1;

    json after = tempoPointsToJson(view->project->tempoCurve.points);
    if (dragState->before.dump() != after.dump())
        view->project->um->newAction(
            new TempoCurveEditAction(view->project, std::move(dragState->before), std::move(after)));

    delete dragState;
    dragState = nullptr;
}

void Transport::render(SDL_Renderer* renderer) {
    const float barY = view->dstRect->y + view->topMargin - Transport::kTimelineHeight;

    // Tempo curve area background
    float areaH = view->topMargin - kTimelineHeight;
    SDL_SetRenderDrawColor(renderer, 40, 40, 45, 255);
    SDL_FRect topBg{view->dstRect->x, view->dstRect->y, view->dstRect->w, areaH};
    SDL_RenderFillRect(renderer, &topBg);

    // Tempo curve
    updateTempoCurveBounds();
    tempoCurveEditor->updateArea(view->dstRect->y, areaH);
    tempoCurveEditor->hoveredPointIdx = tempoCurveEditor->hitTestPoint(mouseX, mouseY);
    tempoCurveEditor->hoveredTensionSeg = tempoCurveEditor->hitTestTension(mouseX, mouseY);
    tempoCurveEditor->render(renderer);

    // Timeline bar
    uint8_t barColor[4] = {35, 35, 40, 255};
    SDL_SetRenderDrawColor(renderer, barColor[0], barColor[1], barColor[2], barColor[3]);
    SDL_FRect barRect{view->dstRect->x, barY, view->dstRect->w, Transport::kTimelineHeight};
    SDL_RenderFillRect(renderer, &barRect);

    for (const auto& rl : view->rhythmLines) {
        float x = view->dstRect->x + view->getX(rl.seconds);
        if (x < view->dstRect->x || x > view->dstRect->x + view->dstRect->w) continue;
        if (rl.isBeat) {
            SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
            SDL_FRect lineRect{x - kBeatLineW / 2.f, barY, kBeatLineW, Transport::kTimelineHeight};
            SDL_RenderFillRect(renderer, &lineRect);
        } else {
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
            SDL_RenderLine(renderer, x, barY, x, barY + Transport::kTimelineHeight);
        }
    }
}
