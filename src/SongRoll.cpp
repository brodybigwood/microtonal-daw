#include "SongRoll.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>
#include "GridElement.h"
#include "GridView.h"
#include "PianoRollWindow.h"
#include "WindowManager.h"
#include "NodeProcessor.h"
#include <SDL3/SDL_events.h>
#include "Region.h"
#include "AudioClip.h"
#include "Transport.h"
#include "ElementManager.h"
#include "SDL_Events.h"
#include "PianoRoll.h"
#include "NodeProcessor.h"
#include "NodeEditor.h"
#include "CurveEditor.h"
#include "AutomationCurve.h"
#include "Note.h"
#include "Preferences.h"
#include "UndoManager.h"
#include "ContextMenu.h"
#include "TreeEntry.h"
#include "nodes/arranger/arranger.h"
#include "NodeManager.h"
#include "Note.h"
#include "PianoRollInternal.h"

namespace {
constexpr float kTimelinePosBorderH = 4.0f;
constexpr float kTimelinePosBorderV = 4.0f;
constexpr float kDragBarH = 14.f;
constexpr float kDragBarResizeW = 10.f;
/** Half-width of resize grab zone from each vertical edge (screen px). */
constexpr float kPositionResizeEdgePx = 5.0f;

float secondsFromScreenX(float screenX, float scrollX, float leftMargin, double dW) {
    return static_cast<float>((static_cast<double>(screenX) + static_cast<double>(scrollX) - static_cast<double>(leftMargin)) / dW);
}

std::vector<std::pair<int, int>> pairsForSeconds(const std::vector<RhythmGridLine>& lines, float seconds) {
    size_t idx = closestRhythmLineIndexForSeconds(lines, seconds);
    if (idx != SIZE_MAX && idx < lines.size())
        return lines[idx].integerPairs;
    return {};
}
} // namespace

void SongRoll::clearPianoRoll(int regionId, bool createUndo) {
    for (size_t i = 0; i < pianoRolls.size(); ) {
        if (pianoRolls[i]->region && static_cast<int>(pianoRolls[i]->region->id) == regionId) {
            PianoRoll* pr = pianoRolls[i];
            if (createUndo && project && project->um && parentNode) {
                auto* action = new TogglePianoRollWindowAction(project,
                    parentNode->nm->managerPath, static_cast<int>(parentNode->id),
                    static_cast<int>(regionId), 0,
                    0.f, 0.f, 0.f, 0.f, 0, false);
                project->um->newAction(action);
            }
            if (i < pianoRollWindows.size() && project && project->processor) {
                auto* wm = project->processor->getWindowManager();
                ExpandedWindow* ew = pianoRollWindows[i];
                pianoRollWindows.erase(pianoRollWindows.begin() + static_cast<ptrdiff_t>(i));
                if (wm) wm->removeWindow(ew);
            }
            pianoRolls.erase(pianoRolls.begin() + static_cast<ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void SongRoll::validateTimelinePointers() {
    for (size_t i = 0; i < pianoRolls.size(); ) {
        PianoRoll* pr = pianoRolls[i];
        if (!pr->region || !em->ids.count(pr->region->id)) {
            if (i < pianoRollWindows.size() && project && project->processor) {
                auto* wm = project->processor->getWindowManager();
                if (wm) wm->removeWindow(pianoRollWindows[i]);
                pianoRollWindows.erase(pianoRollWindows.begin() + static_cast<ptrdiff_t>(i));
            }
            pianoRolls.erase(pianoRolls.begin() + static_cast<ptrdiff_t>(i));
        } else {
            ++i;
        }
    }

    if (timelineHoverPositionId >= 0) {
        if (!em->ids.count(static_cast<uint16_t>(timelineHoverElementId))) {
            hoveredPosition = nullptr;
            timelineHoverElementId = -1;
            timelineHoverPositionId = -1;
        } else {
            GridElement* ge = em->getElement(static_cast<uint16_t>(timelineHoverElementId));
            hoveredPosition = nullptr;
            for (auto* p : ge->positions) {
                if (p->id == timelineHoverPositionId) {
                    hoveredPosition = p;
                    break;
                }
            }
            if (!hoveredPosition) {
                timelineHoverElementId = -1;
                timelineHoverPositionId = -1;
            }
        }
    }

    if (lmb && timelineDragPositionId >= 0) {
        if (!em->ids.count(static_cast<uint16_t>(timelineDragElementId))) {
            movingPosition = nullptr;
            positionDragKind = PositionDragKind::None;
            lmb = false;
            timelineDragElementId = -1;
            timelineDragPositionId = -1;
        } else {
            GridElement* ge = em->getElement(static_cast<uint16_t>(timelineDragElementId));
            GridElement::Position* resolved = nullptr;
            for (auto* p : ge->positions) {
                if (p->id == timelineDragPositionId) {
                    resolved = p;
                    break;
                }
            }
            if (!resolved) {
                movingPosition = nullptr;
                positionDragKind = PositionDragKind::None;
                lmb = false;
                timelineDragElementId = -1;
                timelineDragPositionId = -1;
            } else {
                movingPosition = resolved;
            }
        }
    }

    // Clean up curve editors whose positions no longer exist
    for (auto it = curveEditors.begin(); it != curveEditors.end(); ) {
        bool found = false;
        for (auto* e : em->elements) {
            for (auto* pos : e->positions) {
                if (pos->id == it->first) { found = true; break; }
            }
            if (found) break;
        }
        if (!found) {
            delete it->second;
            it = curveEditors.erase(it);
        } else {
            ++it;
        }
    }
}

SongRoll::SongRoll(SDL_FRect* rect, Window* w, Project* p, ArrangerNode* n) : GridView(rect, 200, w, p), parentNode(n) {
    this->windowHandler = WindowHandler::instance();

    divHeight = 50;
    minHeight = 1.0f/20;

    UpdateGrid();

    rightMargin = 200;

    rightRect = SDL_FRect{dstRect->x + dstRect->w - rightMargin, dstRect->y + topMargin, rightMargin, dstRect->h - topMargin};

    tracks = n->tracks;
    tracks->mouseX = &mouseX;
    tracks->mouseY = &mouseY;
    tracks->parentNode = parentNode;

    em = n->elements;
    em->dstRect = &rightRect;

    createGridRect();

    tracks->songRoll = this;

    leftRect = SDL_FRect{
        dstRect->x, dstRect->y + topMargin, leftMargin, dstRect->h - topMargin
    };

    tracks->setGeometry(&leftRect, renderer);
    tracks->divHeight = &divHeight;
    tracks->scrollY = &scrollY;
}

bool SongRoll::customTick(SDL_Renderer* renderer) {
    if (!texture) generateTextures(renderer);

    syncLayout();
    validateTimelinePointers();

    updateRhythmLines();

    auto target = SDL_GetRenderTarget(renderer);

    RenderGridTexture(renderer);
    renderElements(renderer);


    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, colors.background[0], colors.background[1], colors.background[2], colors.background[3]);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, target);

    SDL_RenderTexture(renderer, texture, nullptr, dstRect);
    SDL_RenderTexture(renderer,gridTexture,nullptr, dstRect);
    SDL_RenderTexture(renderer,regionTexture,nullptr, dstRect);

    const bool showRhythmPreview = selectingRhythmInterval || rhythmEdoDefineDialogOpen;
    if (showRhythmPreview) {
        const float startSec = selectingRhythmInterval ? rhythmIntervalStartSec : rhythmDialogFrozenStartSec;
        const float endSec = selectingRhythmInterval ? rhythmIntervalEndSec : rhythmDialogFrozenEndSec;
        renderRhythmIntervalPreviewBand(renderer, startSec, endSec);
        renderRhythmIntervalEndLine(renderer, endSec);
    }

    renderMargins(renderer);

    playHead->render(renderer, dW, scrollX);
    return true;
}

void SongRoll::renderMargins(SDL_Renderer* renderer) {
    tracks->render(renderer);
    transport->render(renderer);
    em->render(renderer);
}

void SongRoll::syncLayout() {
    // Sync GridView dimensions from dstRect.
    width = dstRect->w;
    height = dstRect->h;
    x = dstRect->x;
    y = dstRect->y;

    rightRect = SDL_FRect{
        dstRect->x + dstRect->w - rightMargin,
        dstRect->y + topMargin,
        rightMargin,
        dstRect->h - topMargin
    };

    leftRect = SDL_FRect{
        dstRect->x,
        dstRect->y + topMargin,
        leftMargin,
        dstRect->h - topMargin
    };

    // Update gridRect to match current dstRect.
    gridRect = {
        dstRect->x + leftMargin,
        dstRect->y + topMargin,
        dstRect->w - leftMargin - rightMargin,
        dstRect->h - topMargin - bottomMargin
    };

    em->scrollY = scrollY;
}

SongRoll::~SongRoll() {
    for (auto* pw : pianoRollWindows) {
        if (project && project->processor) {
            auto* wm = project->processor->getWindowManager();
            if (wm) {
                for (auto* pw2 : pianoRollWindows) {
                    wm->removeWindow(pw2);
                }
            }
        }
    }
    pianoRollWindows.clear();
    pianoRolls.clear();
    for (auto& [id, ce] : curveEditors) delete ce;
    curveEditors.clear();
}

CurveEditor* SongRoll::getCurveEditor(GridElement::Position* pos) {
    if (!pos) return nullptr;
    auto it = curveEditors.find(pos->id);
    if (it != curveEditors.end()) return it->second;
    auto* ac = static_cast<AutomationCurve*>(pos->element);
    auto* ce = new CurveEditor(ac, this, &pos->rhythmVector, &pos->rhythmEndVector, &pos->startOffsetPairs);
    curveEditors[pos->id] = ce;
    return ce;
}

void SongRoll::destroyCurveEditor(int posId) {
    auto it = curveEditors.find(posId);
    if (it != curveEditors.end()) {
        delete it->second;
        curveEditors.erase(it);
    }
}

json SongRoll::snapshotCurvePoints(AutomationCurve* ac) {
    json pts = json::array();
    for (const auto& pt : ac->points) {
        json jpt;
        jpt["v"] = pt.v;
        jpt["shape"] = static_cast<int>(pt.shape.type);
        jpt["shapeParam"] = pt.shape.param;
        jpt["timeVec"] = json::array();
        for (const auto& pr : pt.timeVec)
            jpt["timeVec"].push_back(json::array({pr.first, pr.second}));
        pts.push_back(jpt);
    }
    return pts;
}

void SongRoll::pushCurveUndo(AutomationCurve* ac, const json& before) {
    if (!project || !project->um || !parentNode) return;
    json after = snapshotCurvePoints(ac);
    if (before != after) {
        project->um->newAction(new ModifyCurvePointsAction(project,
            parentNode->nm->managerPath, static_cast<int>(parentNode->id),
            static_cast<int>(ac->id), before, after));
    }
}

float SongRoll::secondsFromMouseX() {
    return secondsFromScreenX(mouseX, static_cast<float>(scrollX), leftMargin, dW);
}

std::vector<std::pair<int, int>> SongRoll::pairsAtMouseX() {
    return pairsForSeconds(rhythmLines, secondsFromMouseX());
}

void SongRoll::updateRhythmLines() {
    const int steps = parentNode ? parentNode->rhythmEdoSubdivisionSteps : 1;
    static const std::vector<std::pair<int, int>> kOneSec{{1,1}};
    const auto& lower = parentNode ? parentNode->rhythmEdoLowerVector : std::vector<std::pair<int,int>>{};
    const auto& upper = (parentNode && !parentNode->rhythmEdoUpperVector.empty())
        ? parentNode->rhythmEdoUpperVector : kOneSec;
    const float minSec = (static_cast<float>(scrollX) - leftMargin) / static_cast<float>(dW);
    const float maxSec = (static_cast<float>(scrollX) + width - leftMargin) / static_cast<float>(dW);
    generateRhythmLines(rhythmLines, rhythmLineLabels, steps, lower, upper, minSec, maxSec);
    times.clear();
    for (const auto& rl : rhythmLines)
        times.push_back(rl.seconds);
}

void SongRoll::refreshHoveredRhythmLineIndex() {
    hoveredRhythmLineIndex = closestRhythmLineIndexForSeconds(rhythmLines, secondsFromMouseX());
}

void SongRoll::RenderGridTexture(SDL_Renderer* renderer) {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    setRenderColor(renderer, colors.grid);

    for (const auto& rl : rhythmLines) {
        float x = getX(rl.seconds);
        if (x < 0 || x > width) continue;
        setRenderColor(renderer, colors.grid);
        if (rl.isBeat) {
            SDL_FRect lineRect{x - 1.f, 0, 2.f, static_cast<float>(height)};
            SDL_RenderFillRect(renderer, &lineRect);
        } else {
            SDL_RenderLine(renderer, x, 0, x, static_cast<float>(height));
        }
    }

    for (auto line : lines) {
        float val = getY(line);
        SDL_RenderLine(renderer, 0, val, width, val);
    }
    SDL_SetRenderTarget(renderer, target);
}

void SongRoll::movePosition() {
    if (!movingPosition) return;

    const float oldSec = secondsFromScreenX(last_lmb_x, static_cast<float>(scrollX), leftMargin, dW);
    const float newSec = secondsFromMouseX();
    const float deltaSec = newSec - oldSec;

    switch (positionDragKind) {
        case PositionDragKind::ResizeLeft: {
            size_t li = closestRhythmLineIndexForSeconds(rhythmLines, secondsFromMouseX());
            if (li != SIZE_MAX && li < rhythmLines.size())
                movingPosition->rhythmVector = rhythmLines[li].integerPairs;
            auto startDelta = subVec(movingPosition->rhythmVector, lastPosition.rhythmVector);
            movingPosition->startOffsetPairs = addVec(lastPosition.startOffsetPairs, startDelta);
            if (parentNode) {
                movingPosition->rhythmEdoSubdivisionSteps = parentNode->rhythmEdoSubdivisionSteps;
                movingPosition->rhythmEdoLowerVector = parentNode->rhythmEdoLowerVector;
                movingPosition->rhythmEdoUpperVector = parentNode->rhythmEdoUpperVector;
            }
            break;
        }
        case PositionDragKind::ResizeRight: {
            float startSec = Note::secondsFromVector(lastPosition.rhythmVector);
            float newEndSec = Note::secondsFromVector(lastPosition.rhythmEndVector) + deltaSec;
            const float minLenSec = 1.0f / static_cast<float>(std::max(1, parentNode ? parentNode->rhythmEdoSubdivisionSteps : 1));
            if (newEndSec - startSec < minLenSec)
                newEndSec = startSec + minLenSec;
            size_t li = closestRhythmLineIndexForSeconds(rhythmLines, newEndSec);
            if (li != SIZE_MAX && li < rhythmLines.size())
                movingPosition->rhythmEndVector = rhythmLines[li].integerPairs;
            movingPosition->rhythmVector = lastPosition.rhythmVector;
            if (parentNode) {
                movingPosition->rhythmEdoSubdivisionSteps = parentNode->rhythmEdoSubdivisionSteps;
                movingPosition->rhythmEdoLowerVector = parentNode->rhythmEdoLowerVector;
                movingPosition->rhythmEdoUpperVector = parentNode->rhythmEdoUpperVector;
            }
            break;
        }
        case PositionDragKind::Move:
        default: {
            float startSec = Note::secondsFromVector(lastPosition.rhythmVector) + deltaSec;
            size_t sli = closestRhythmLineIndexForSeconds(rhythmLines, startSec);
            if (sli != SIZE_MAX && sli < rhythmLines.size()) {
                auto startDelta = subVec(rhythmLines[sli].integerPairs, lastPosition.rhythmVector);
                movingPosition->rhythmVector = rhythmLines[sli].integerPairs;
                movingPosition->rhythmEndVector = addVec(lastPosition.rhythmEndVector, startDelta);
            }
            if (parentNode) {
                movingPosition->rhythmEdoSubdivisionSteps = parentNode->rhythmEdoSubdivisionSteps;
                movingPosition->rhythmEdoLowerVector = parentNode->rhythmEdoLowerVector;
                movingPosition->rhythmEdoUpperVector = parentNode->rhythmEdoUpperVector;
            }
            break;
        }
    }

    if (positionDragKind == PositionDragKind::Move) {
        int trackID = getHoveredTrack();
        auto track = tracks->getTrack(trackID);
        auto oldTrack = tracks->getTrack(lastPosition.trackID);
        if (track && oldTrack && track->type == oldTrack->type)
            movingPosition->trackID = trackID;
    }
}

void SongRoll::handleCustomInput(SDL_Event& e) {

    em->mouseX = mouseX;
    em->mouseY = mouseY;

    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            getHoveredPosition();
            refreshHoveredRhythmLineIndex();

            // Curve point hover (only when not dragging)
            if (draggingCurvePointIdx < 0 && draggingTensionSeg < 0) {
                hoveredCurvePointIdx = -1;
                activeCurveEditorPosId = -1;
            }
            if (hoveredCurvePointIdx < 0) {
                for (auto& [id, ce] : curveEditors) ce->hoveredTensionSeg = -1;
            }
            if (hoveredPosition && hoveredPosition->element->type == ElementType::automationCurve &&
                lmb == false && positionDragKind == PositionDragKind::None) {
                auto* ce = getCurveEditor(hoveredPosition);
                if (ce) {
                    ce->updateArea(getY(tracks->getIndex(hoveredPosition->trackID)) + kDragBarH, divHeight - kDragBarH);
                    int pt = ce->hitTestPoint(mouseX, mouseY);
                    if (pt >= 0) {
                        hoveredCurvePointIdx = pt;
                        activeCurveEditorPosId = hoveredPosition->id;
                    }
                    ce->hoveredPointIdx = hoveredCurvePointIdx;
                    ce->hoveredTensionSeg = ce->hitTestTension(mouseX, mouseY);
                }
            }

            // Tension handle drag
            if (lmb && draggingTensionSeg >= 0 && activeCurveEditorPosId >= 0) {
                auto it = curveEditors.find(activeCurveEditorPosId);
                if (it != curveEditors.end()) {
                    auto* ce = it->second;
                    if (draggingTensionSeg < static_cast<int>(ce->curve()->points.size()) - 1) {
                        float dy = mouseY - tensionDragStartY;
                        auto& segPt = ce->curve()->points[draggingTensionSeg];
                        float dir = ce->curve()->points[draggingTensionSeg + 1].v >= segPt.v ? 1.f : -1.f;
                        segPt.shape.param = std::max(-1.f, std::min(1.f, tensionDragStartParam - dy * dir / (ce->area.h * 0.5f)));
                        refreshGrid = true;
                    }
                }
            }

            if (selectingRhythmInterval) {
                refreshHoveredRhythmLineIndex();
                if (hoveredRhythmLineIndex != SIZE_MAX && hoveredRhythmLineIndex < rhythmLines.size()) {
                    rhythmIntervalEndSec = rhythmLines[hoveredRhythmLineIndex].seconds;
                    rhythmDragEndVertexPairs = rhythmLines[hoveredRhythmLineIndex].integerPairs;
                    if (std::fabs(rhythmIntervalEndSec - rhythmIntervalStartSec) > 0.001f)
                        rhythmIntervalDragMoved = true;
                }
                refreshGrid = true;
                break;
            }

            // Curve point drag
            if (lmb && draggingCurvePointIdx >= 0 && activeCurveEditorPosId >= 0) {
                auto it = curveEditors.find(activeCurveEditorPosId);
                if (it != curveEditors.end()) {
                    auto* ce = it->second;
                    auto* ac = ce->curve();
                    if (draggingCurvePointIdx < static_cast<int>(ac->points.size())) {
                        // Find the position to compute area
                        for (auto* e : em->elements) {
                            for (auto* p : e->positions) {
                                if (p->id == activeCurveEditorPosId) {
                                    ce->updateArea(getY(tracks->getIndex(p->trackID)) + kDragBarH, divHeight - kDragBarH);
                                    break;
                                }
                            }
                        }
                        auto& pt = ac->points[draggingCurvePointIdx];
                        refreshHoveredRhythmLineIndex();
                        if (hoveredRhythmLineIndex != SIZE_MAX && hoveredRhythmLineIndex < rhythmLines.size()) {
                            auto newVec = addVec(subVec(rhythmLines[hoveredRhythmLineIndex].integerPairs, *ce->startVec()), *ce->offsetVec());
                            float newSec = Note::secondsFromVector(newVec);
                            // Don't cross adjacent points
                            if (draggingCurvePointIdx > 0) {
                                float prevSec = Note::secondsFromVector(ac->points[draggingCurvePointIdx - 1].timeVec);
                                if (newSec <= prevSec) newVec = ac->points[draggingCurvePointIdx - 1].timeVec;
                            }
                            if (draggingCurvePointIdx < static_cast<int>(ac->points.size()) - 1) {
                                float nextSec = Note::secondsFromVector(ac->points[draggingCurvePointIdx + 1].timeVec);
                                if (newSec >= nextSec) newVec = ac->points[draggingCurvePointIdx + 1].timeVec;
                            }
                            pt.timeVec = newVec;
                        }
                        if (ce->area.h > 0.f) {
                            float relY = (mouseY - ce->area.y) / ce->area.h;
                            pt.v = std::max(0.f, std::min(1.f, 1.f - relY));
                        }
                        refreshGrid = true;
                    }
                }
            }

            movePosition();
            if (mouseX < rightRect.x) {
                const bool resizing = lmb && (positionDragKind == PositionDragKind::ResizeLeft ||
                                              positionDragKind == PositionDragKind::ResizeRight);
                bool overResize = resizing;
                if (!overResize && hoveredPosition) {
                    const float xL = getX(Note::secondsFromVector(hoveredPosition->rhythmVector));
                    const float xR = getX(Note::secondsFromVector(hoveredPosition->rhythmEndVector));
                    const float yT = getY(tracks->getIndex(hoveredPosition->trackID));
                    if (mouseY >= yT && mouseY <= yT + kDragBarH) {
                        if (mouseX <= xL + kDragBarResizeW || mouseX >= xR - kDragBarResizeW)
                            overResize = true;
                    }
                }
                if (overResize)
                    SDL_SetCursor(cursors.resize);
                else if (lmb && positionDragKind == PositionDragKind::Move)
                    SDL_SetCursor(cursors.mover);
                else
                    SDL_SetCursor(cursors.selector);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if(scrollY < 0) {
                scrollY = 0;
            }
            break;
        default:
            break;
    }
    if (mouseX > rightRect.x && mouseX <= (rightRect.x + rightRect.w) &&
        mouseY > rightRect.y && mouseY <= (rightRect.y + rightRect.h)) {
        em->handleInput(e);
    }

    if (mouseX > leftRect.x && mouseX <= (leftRect.x + leftRect.w) &&
        mouseY > leftRect.y && mouseY <= (leftRect.y + leftRect.h)) {
        tracks->handleInput(e);
    } else tracks->hoveredTrack = nullptr;

}

void SongRoll::renderElements(SDL_Renderer* renderer) {
    SDL_SetRenderTarget(renderer, regionTexture);
    SDL_SetRenderDrawColor(renderer, 0,0,0,0);
    SDL_RenderClear(renderer);
    for (auto element : em->elements) {
        renderElement(renderer, element);
    }

    renderDrop(renderer);
}

void SongRoll::renderElement(SDL_Renderer* renderer, GridElement* element) {
    // Use seconds directly — dW is now pixels-per-second
    element->draw(renderer, dW, static_cast<int>(divHeight));
    SDL_SetRenderTarget(renderer, regionTexture);
    for(auto position : element->positions) {
        auto& pos = *position;
        if(hoveredPosition == &pos) {
            SDL_SetRenderDrawColor(renderer, 90,90,100,127);
        } else {
            SDL_SetRenderDrawColor(renderer, 20,20,100,127);
        }

        const float startSec = Note::secondsFromVector(pos.rhythmVector);
        const float endSec = Note::secondsFromVector(pos.rhythmEndVector);
        float offSec = Note::secondsFromVector(pos.startOffsetPairs);
        uint16_t index = tracks->getIndex(pos.trackID);
        float topLeftCornerY = getY(index);
        float fullW = (endSec - startSec) * dW;
        // Box spans [startSec, endSec]; texture is anchored offSec earlier and clipped to the box,
        // so the content visible at the box's left edge is element-second offSec.
        const float texLenSec = (endSec - startSec) + offSec;
        SDL_FRect visRect = {getX(startSec), topLeftCornerY, fullW, divHeight};
        SDL_FRect fullDst = {getX(startSec - offSec), topLeftCornerY, texLenSec * dW, divHeight};

        SDL_FRect srcRect;
        switch (element->type) {
            case ElementType::region:
                srcRect = {0, 0, texLenSec * 100.0f, 100.0f};
                break;
            case ElementType::audioClip:
                srcRect = {0, 0, fullDst.w, fullDst.h};
                break;
            default:
                break;
        }

        SDL_RenderFillRect(renderer, &visRect);
        SDL_Rect sdlClip = {(int)visRect.x, (int)visRect.y, (int)visRect.w, (int)visRect.h};
        SDL_SetRenderClipRect(renderer, &sdlClip);
        SDL_RenderTexture(renderer, element->texture, &srcRect, &fullDst);
        SDL_SetRenderClipRect(renderer, nullptr);

        float topLeftCornerX = visRect.x;
        float wPx = visRect.w;

        const float x0 = topLeftCornerX;
        const float y0 = topLeftCornerY;
        const float hPx = divHeight;

        // Drag bar (move handle) at top
        SDL_SetRenderDrawColor(renderer, 80, 84, 94, 255);
        SDL_FRect dragBar{x0, y0, wPx, kDragBarH};
        SDL_RenderFillRect(renderer, &dragBar);
        // Drag bar outline
        SDL_SetRenderDrawColor(renderer, 140, 145, 155, 255);
        SDL_RenderRect(renderer, &dragBar);
        // Resize handles on left/right of drag bar
        SDL_SetRenderDrawColor(renderer, 120, 200, 255, 180);
        SDL_FRect rszL{x0, y0, kDragBarResizeW, kDragBarH};
        SDL_RenderFillRect(renderer, &rszL);
        SDL_SetRenderDrawColor(renderer, 255, 190, 140, 180);
        SDL_FRect rszR{x0 + wPx - kDragBarResizeW, y0, kDragBarResizeW, kDragBarH};
        SDL_RenderFillRect(renderer, &rszR);

        const float contentY = y0 + kDragBarH;
        const float contentH = std::max(0.0f, hPx - kDragBarH);

        // Bottom edge
        SDL_SetRenderDrawColor(renderer, 52, 54, 62, 255);
        SDL_FRect hBot{x0, y0 + hPx - kTimelinePosBorderH, wPx, kTimelinePosBorderH};
        SDL_RenderFillRect(renderer, &hBot);

        // Render curve for automation positions
        if (element->type == ElementType::automationCurve) {
            auto* ce = getCurveEditor(position);
            if (ce) {
                ce->updateArea(getY(tracks->getIndex(position->trackID)) + kDragBarH, divHeight - kDragBarH);
                ce->render(renderer);
            }
        }
    }
}

void SongRoll::getHoveredPosition() {
    for (auto e : em->elements) {
        for(auto position :e->positions) {
            auto& pos = *position;
            uint16_t index = tracks->getIndex(pos.trackID);
            const float startSec = Note::secondsFromVector(pos.rhythmVector);
            const float endSec = Note::secondsFromVector(pos.rhythmEndVector);
            if(
                mouseX < rightRect.x &&
                mouseX > getX(startSec) &&
                mouseX < getX(endSec) &&
                mouseY > getY(index) &&
                mouseY < getY(index+1)
            ) {
                hoveredPosition = &pos;
                timelineHoverElementId = static_cast<int>(e->id);
                timelineHoverPositionId = pos.id;
                return;
            }
        }
    }
    hoveredPosition = nullptr;
    timelineHoverElementId = -1;
    timelineHoverPositionId = -1;
}

float SongRoll::getHoveredLine() {
    return (mouseY + scrollY - topMargin)/divHeight;
}

int SongRoll::getHoveredTrack() {
    auto trackIndex = getHoveredLine();
    return tracks->getID(trackIndex);
}

void SongRoll::createElement() {
    int id = em->currentElement;

    if (id == -1 || !em->ids.count(static_cast<uint16_t>(id))) {
        em->currentElement = -1;
        return;
    }
    auto startPairs = pairsAtMouseX();
    auto endPairs = lastPositionDurationPairs.empty()
        ? addVec(startPairs, std::vector<std::pair<int,int>>{{2,1}}) // default 2s
        : addVec(startPairs, lastPositionDurationPairs);

    auto trackID = getHoveredTrack();
    auto track = tracks->getTrack(trackID);
    if (!track) return;

    auto elem = em->getElement(id);

    if (track->getType() == TrackType::Notes && elem->type != ElementType::region) return;
    if (elem->type == ElementType::region && track->getType() != TrackType::Notes) return;

    project->um->newAction(new CreatePositionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id),
        static_cast<int>(id), startPairs, endPairs, static_cast<uint16_t>(trackID), lastPositionStartOffsetPairs));

    if (elem && !elem->positions.empty()) {
        auto* pos = elem->positions.back();
        lastPositionDurationPairs = subVec(pos->rhythmEndVector, pos->rhythmVector);
        lastPositionStartOffsetPairs = pos->startOffsetPairs;
        em->currentElement = id;
    }

    refreshGrid = true;
}

void SongRoll::doubleClick() {
    if (hoveredPosition) {
        auto e = hoveredPosition->element;
        if (e->type == ElementType::region) {
            auto reg = static_cast<Region*>(e);
            movingPosition = nullptr;
            positionDragKind = PositionDragKind::None;
            lmb = false;
            createPianoRoll(reg, false);
        }
    } else {
        auto trackID = getHoveredTrack();
        auto track = tracks->getTrack(trackID);
        if (track && track->type == TrackType::Notes) {
            auto startPairs = pairsAtMouseX();
            auto endPairs = lastPositionDurationPairs.empty()
                ? addVec(startPairs, std::vector<std::pair<int,int>>{{2,1}})
                : addVec(startPairs, lastPositionDurationPairs);
            auto* cra = new CreateRegionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id));
            project->um->newAction(cra);
            project->um->newAction(new CreatePositionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id),
                cra->regionID, startPairs, endPairs, static_cast<uint16_t>(trackID), lastPositionStartOffsetPairs));
            auto* elem = em->getElement(cra->regionID);
            if (elem && !elem->positions.empty()) {
                auto* pos = elem->positions.back();
                lastPositionDurationPairs = subVec(pos->rhythmEndVector, pos->rhythmVector);
                lastPositionStartOffsetPairs = pos->startOffsetPairs;
                em->currentElement = cra->regionID;
            }
            refreshGrid = true;
        }
    }
}

void SongRoll::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:

            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                last_lmb_x = mouseX;
                last_lmb_y = mouseY;

                // Ctrl+Shift: start rhythm interval drag
                if (isCtrlPressed && isShiftPressed && mouseX > leftMargin && mouseX < rightRect.x) {
                    rhythmEdoDefineDialogOpen = false;
                    selectingRhythmInterval = true;
                    refreshHoveredRhythmLineIndex();
                    if (hoveredRhythmLineIndex != SIZE_MAX && hoveredRhythmLineIndex < rhythmLines.size()) {
                        rhythmIntervalStartSec = rhythmLines[hoveredRhythmLineIndex].seconds;
                        rhythmDragStartVertexPairs = rhythmLines[hoveredRhythmLineIndex].integerPairs;
                    } else {
                        rhythmIntervalStartSec = 0.0f;
                        rhythmDragStartVertexPairs.clear();
                    }
                    rhythmIntervalEndSec = rhythmIntervalStartSec;
                    rhythmDragEndVertexPairs.clear();
                    rhythmIntervalDragMoved = false;
                    return;
                }

                // Curve point interaction for automation positions (below drag bar only)
                {
                    GridElement::Position* curvePos = hoveredPosition;
                    if (!curvePos || curvePos->element->type != ElementType::automationCurve) {
                        for (auto* e : em->elements) {
                            if (e->type != ElementType::automationCurve) continue;
                            for (auto* p : e->positions) {
                                float xL = getX(Note::secondsFromVector(p->rhythmVector));
                                float xR = getX(Note::secondsFromVector(p->rhythmEndVector));
                                float yT = getY(tracks->getIndex(p->trackID)) + kDragBarH;
                                float yB = yT + divHeight - kDragBarH;
                                if (mouseX >= xL - CurveEditor::kHandleRadius * 3 &&
                                    mouseX <= xR + CurveEditor::kHandleRadius * 3 &&
                                    mouseY >= yT && mouseY <= yB) {
                                    curvePos = p;
                                    break;
                                }
                            }
                            if (curvePos && curvePos->element->type == ElementType::automationCurve) break;
                        }
                    }
                    // Only handle curve interaction below the drag bar
                    bool inCurveZone = curvePos && curvePos->element->type == ElementType::automationCurve;
                    if (inCurveZone) {
                        float yT = getY(tracks->getIndex(curvePos->trackID));
                        if (mouseY < yT + kDragBarH) inCurveZone = false;
                    }
                    if (inCurveZone) {
                        auto* ce = getCurveEditor(curvePos);
                        if (ce) {
                            ce->updateArea(getY(tracks->getIndex(curvePos->trackID)) + kDragBarH, divHeight - kDragBarH);
                            float ax = ce->area.x;
                            float ay = ce->area.y;
                            float aw = ce->area.w;
                            float ah = ce->area.h;
                            int tensSeg = ce->hitTestTension(mouseX, mouseY);
                            if (tensSeg >= 0) {
                                draggingTensionSeg = tensSeg;
                                tensionDragStartY = mouseY;
                                tensionDragStartParam = ce->curve()->points[tensSeg].shape.param;
                                activeCurveEditorPosId = curvePos->id;
                                auto* ac = static_cast<AutomationCurve*>(curvePos->element);
                                curveDragUndoBefore = snapshotCurvePoints(ac);
                                return;
                            }
                            int pt = ce->hitTestPoint(mouseX, mouseY);
                            if (pt >= 0) {
                                draggingCurvePointIdx = pt;
                                activeCurveEditorPosId = curvePos->id;
                                auto* ac = static_cast<AutomationCurve*>(curvePos->element);
                                curveDragUndoBefore = snapshotCurvePoints(ac);
                                return;
                            }
                            // Add point anywhere in the curve area
                            if (mouseX >= ax && mouseX <= ax + aw &&
                                mouseY >= ay && mouseY <= ay + ah) {
                                refreshHoveredRhythmLineIndex();
                                if (hoveredRhythmLineIndex == SIZE_MAX || hoveredRhythmLineIndex >= rhythmLines.size())
                                    return;
                                float v = std::max(0.f, std::min(1.f, 1.f - (mouseY - ay) / ah));
                                auto* ac = static_cast<AutomationCurve*>(curvePos->element);
                                auto before = snapshotCurvePoints(ac);
                                auto relVec = addVec(subVec(rhythmLines[hoveredRhythmLineIndex].integerPairs, curvePos->rhythmVector), curvePos->startOffsetPairs);
                                ac->addPoint(relVec, v, CurveShape::Single);
                                pushCurveUndo(ac, before);
                                refreshGrid = true;
                                return;
                            }
                        }
                    }
                }

                positionDragKind = PositionDragKind::None;
                movingPosition = hoveredPosition;
                timelineDragElementId = -1;
                timelineDragPositionId = -1;
                if (movingPosition) {
                    const float xL = getX(Note::secondsFromVector(movingPosition->rhythmVector));
                    const float xR = getX(Note::secondsFromVector(movingPosition->rhythmEndVector));
                    const float yT = getY(tracks->getIndex(movingPosition->trackID));
                    const float barY = yT;
                    const float barH = kDragBarH;
                    if (mouseY >= barY && mouseY <= barY + barH) {
                        if (mouseX <= xL + kDragBarResizeW)
                            positionDragKind = PositionDragKind::ResizeLeft;
                        else if (mouseX >= xR - kDragBarResizeW)
                            positionDragKind = PositionDragKind::ResizeRight;
                        else
                            positionDragKind = PositionDragKind::Move;
                    } else {
                        movingPosition = nullptr;
                    }
                }
                if (movingPosition) {
                    lastPosition = *movingPosition;
                    timelineDragElementId = static_cast<int>(movingPosition->element->id);
                    timelineDragPositionId = movingPosition->id;
                }

                if (SDL_GetTicks() - lastLmbTime < DCT) doubleClick();
                else if (!hoveredPosition &&
                    mouseX > gridRect.x && mouseX < gridRect.x + gridRect.w &&
                    mouseY > gridRect.y && mouseY < gridRect.y + gridRect.h) {
                    createElement();
                }

                lastLmbTime = SDL_GetTicks();
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                // Curve point deletion
                if (hoveredCurvePointIdx >= 0 && hoveredPosition &&
                    hoveredPosition->element->type == ElementType::automationCurve) {
                    auto* ac = static_cast<AutomationCurve*>(hoveredPosition->element);
                    int ptIdx = hoveredCurvePointIdx;
                    auto menuRoot = std::make_shared<TreeEntry>("Point " + std::to_string(ptIdx));
                    auto holdEntry = std::make_shared<TreeEntry>("Hold");
                    auto singleEntry = std::make_shared<TreeEntry>("Single");
                    auto doubleEntry = std::make_shared<TreeEntry>("Double");
                    auto deleteEntry = std::make_shared<TreeEntry>("Delete");
                    holdEntry->click = [this, ac, ptIdx]() {
                        auto before = snapshotCurvePoints(ac);
                        ac->points[ptIdx].shape.type = CurveShape::Hold;
                        pushCurveUndo(ac, before);
                        refreshGrid = true;
                    };
                    singleEntry->click = [this, ac, ptIdx]() {
                        auto before = snapshotCurvePoints(ac);
                        ac->points[ptIdx].shape.type = CurveShape::Single;
                        pushCurveUndo(ac, before);
                        refreshGrid = true;
                    };
                    doubleEntry->click = [this, ac, ptIdx]() {
                        auto before = snapshotCurvePoints(ac);
                        ac->points[ptIdx].shape.type = CurveShape::Double;
                        pushCurveUndo(ac, before);
                        refreshGrid = true;
                    };
                    deleteEntry->click = [this, ac, ptIdx]() {
                        auto before = snapshotCurvePoints(ac);
                        ac->removePoint(static_cast<size_t>(ptIdx));
                        pushCurveUndo(ac, before);
                        refreshGrid = true;
                    };
                    menuRoot->addChild(holdEntry);
                    menuRoot->addChild(singleEntry);
                    menuRoot->addChild(doubleEntry);
                    menuRoot->addChild(deleteEntry);
                    auto ctxMenu = ContextMenu::get();
                    ctxMenu->skipNextEvent = true;
                    ctxMenu->activate();
                    ctxMenu->dynamicTick = getTreeMenuTicker(menuRoot);
                    rmb = false;
                    return;
                }
                if (hoveredPosition && mouseX < rightRect.x) {
                    GridElement* el = hoveredPosition->element;
                    const int elemId = static_cast<int>(el->id);
                    const int posId = hoveredPosition->id;
                    if (el->type == ElementType::region) {
                        auto* reg = static_cast<Region*>(el);
                        if (reg->positions.size() == 1u)
                            clearPianoRoll(static_cast<int>(reg->id));
                    }
                    project->um->newAction(new DeletePositionAction(project, parentNode->nm->managerPath,
                        static_cast<int>(parentNode->id), elemId, posId));
                    refreshGrid = true;
                } else if (MouseOn(&rightRect) && em->hoveredElement != -1 && !em->hoverNewRegion) {
                    GridElement* ge = em->getElement(static_cast<uint16_t>(em->hoveredElement));
                    if (ge->type == ElementType::region) {
                        auto* ctxMenu = ContextMenu::get();
                        ctxMenu->activate();
                        const uint16_t rid = ge->id;
                        auto root = uTreeEntry();
                        auto del = uTreeEntry();
                        del->label = "Delete Region";
                        del->click = [this, rid]() {
                            auto* reg = static_cast<Region*>(em->getElement(rid));
                            clearPianoRoll(static_cast<int>(rid));
                            project->um->newAction(new DeleteRegionAction(project, parentNode->nm->managerPath,
                                static_cast<int>(parentNode->id), static_cast<int>(rid)));
                            refreshGrid = true;
                        };
                        root->addChild(del);
                        ctxMenu->dynamicTick = getTreeMenuTicker(root);
                    }
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                // Ctrl+Shift: finish rhythm interval drag
                if (selectingRhythmInterval) {
                    selectingRhythmInterval = false;
                    rhythmEdoDefineDialogOpen = false;
                    if (std::fabs(rhythmIntervalEndSec - rhythmIntervalStartSec) < 0.001f) {
                        // Ctrl+Shift+click on a position without dragging: copy its rhythm EDO to arranger
                        if (hoveredPosition && parentNode) {
                            json before;
                            before["steps"] = parentNode->rhythmEdoSubdivisionSteps;
                            before["lower"] = json::array();
                            for (const auto& pr : parentNode->rhythmEdoLowerVector)
                                before["lower"].push_back(json::array({pr.first, pr.second}));
                            before["upper"] = json::array();
                            for (const auto& pr : parentNode->rhythmEdoUpperVector)
                                before["upper"].push_back(json::array({pr.first, pr.second}));
                            parentNode->rhythmEdoSubdivisionSteps = hoveredPosition->rhythmEdoSubdivisionSteps;
                            parentNode->rhythmEdoLowerVector = hoveredPosition->rhythmEdoLowerVector;
                            parentNode->rhythmEdoUpperVector = hoveredPosition->rhythmEdoUpperVector;
                            updateRhythmLines();
                            json after;
                            after["steps"] = hoveredPosition->rhythmEdoSubdivisionSteps;
                            after["lower"] = json::array();
                            for (const auto& pr : hoveredPosition->rhythmEdoLowerVector)
                                after["lower"].push_back(json::array({pr.first, pr.second}));
                            after["upper"] = json::array();
                            for (const auto& pr : hoveredPosition->rhythmEdoUpperVector)
                                after["upper"].push_back(json::array({pr.first, pr.second}));
                            project->um->newAction(new SongRollRhythmEdoAction(project,
                                parentNode->nm->managerPath, static_cast<int>(parentNode->id),
                                before, after));
                        }
                        refreshGrid = true;
                    } else {
                        rhythmDialogFrozenStartSec = rhythmIntervalStartSec;
                        rhythmDialogFrozenEndSec = rhythmIntervalEndSec;
                        rhythmDialogFrozenStartPairs = rhythmDragStartVertexPairs;
                        rhythmDialogFrozenEndPairs = rhythmDragEndVertexPairs;
                        rhythmEdoDefineDialogOpen = true;
                        const float a = rhythmDialogFrozenStartSec;
                        const float b = rhythmDialogFrozenEndSec;
                        const auto capStartPairs = rhythmDialogFrozenStartPairs;
                        const auto capEndPairs = rhythmDialogFrozenEndPairs;
                        auto ctxMenu = ContextMenu::get();
                        ctxMenu->skipNextEvent = true;
                        if (project && project->window)
                            SDL_StartTextInput(project->window);
                        ctxMenu->activate();
                        ctxMenu->dynamicTick = getTextInputTicker(
                            [this, a, b, capStartPairs, capEndPairs](std::string text) {
                            try {
                                int num = 1, den = 1;
                                auto slash = text.find('/');
                                if (slash != std::string::npos) {
                                    num = std::max(1, std::stoi(text.substr(0, slash)));
                                    den = std::max(1, std::stoi(text.substr(slash + 1)));
                                    int g = std::gcd(num, den);
                                    num /= g;
                                    den /= g;
                                } else {
                                    num = std::max(1, std::stoi(text));
                                }
                                auto diffVec = subVec(capEndPairs, capStartPairs);
                                std::vector<std::pair<int, int>> scaledDiff;
                                scaledDiff.reserve(diffVec.size());
                                for (auto& p : diffVec)
                                    scaledDiff.push_back(ratMulInt(p, den));
                                auto otherPairs = addVec(capStartPairs, scaledDiff);
                                const float fixedSec = a;
                                const float otherSec = Note::secondsFromVector(otherPairs);
                                const auto& lowerPairs = (fixedSec <= otherSec) ? capStartPairs : otherPairs;
                                const auto& upperPairs = (fixedSec <= otherSec) ? otherPairs : capStartPairs;
                                int steps = num;
                                json before;
                                before["steps"] = parentNode->rhythmEdoSubdivisionSteps;
                                before["lower"] = json::array();
                                for (const auto& pr : parentNode->rhythmEdoLowerVector)
                                    before["lower"].push_back(json::array({pr.first, pr.second}));
                                before["upper"] = json::array();
                                for (const auto& pr : parentNode->rhythmEdoUpperVector)
                                    before["upper"].push_back(json::array({pr.first, pr.second}));
                                parentNode->rhythmEdoSubdivisionSteps = steps;
                                parentNode->rhythmEdoLowerVector = lowerPairs;
                                parentNode->rhythmEdoUpperVector = upperPairs;
                                updateRhythmLines();
                                refreshGrid = true;
                                json after;
                                after["steps"] = steps;
                                after["lower"] = json::array();
                                for (const auto& pr : lowerPairs)
                                    after["lower"].push_back(json::array({pr.first, pr.second}));
                                after["upper"] = json::array();
                                for (const auto& pr : upperPairs)
                                    after["upper"].push_back(json::array({pr.first, pr.second}));
                                project->um->newAction(new SongRollRhythmEdoAction(project,
                                    parentNode->nm->managerPath, static_cast<int>(parentNode->id),
                                    before, after));
                            } catch (...) {}
                        },
                            [this]() { rhythmEdoDefineDialogOpen = false; });
                    }
                }

                if ((draggingCurvePointIdx >= 0 || draggingTensionSeg >= 0) && activeCurveEditorPosId >= 0) {
                    auto it = curveEditors.find(activeCurveEditorPosId);
                    if (it != curveEditors.end()) {
                        auto* ac = it->second->curve();
                        pushCurveUndo(ac, curveDragUndoBefore);
                    }
                }
                draggingCurvePointIdx = -1;
                draggingTensionSeg = -1;
                activeCurveEditorPosId = -1;

                if (movingPosition) {
                    json after = GridElement::positionToJson(*movingPosition);
                    json before = GridElement::positionToJson(lastPosition);
                    if (before != after) {
                        GridElement* el = movingPosition->element;
                        project->um->newAction(new MoveElementPositionAction(project, parentNode->nm->managerPath,
                            static_cast<int>(parentNode->id), static_cast<int>(el->id), movingPosition->id, std::move(before),
                            std::move(after)));
                        lastPositionDurationPairs = subVec(movingPosition->rhythmEndVector, movingPosition->rhythmVector);
                        lastPositionStartOffsetPairs = movingPosition->startOffsetPairs;
                        if (el)
                            em->currentElement = static_cast<int>(el->id);
                        refreshGrid = true;
                    }
                }
                lmb = false;
                movingPosition = nullptr;
                positionDragKind = PositionDragKind::None;
                timelineDragElementId = -1;
                timelineDragPositionId = -1;
                if (mouseX < rightRect.x)
                    SDL_SetCursor(cursors.selector);
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = false;
            }
            break;
    }
}

float SongRoll::getY(float index) {
    return divHeight * index + topMargin - scrollY;
}

void SongRoll::UpdateGrid() {
    lines.clear();
    float y = -20;
    while (y < 20) {
        lines.push_back(y);
        y++;
    }
    updateRhythmLines();
}

void SongRoll::renderDrop(SDL_Renderer* renderer) {
    if (!dropping) return;
    SDL_FRect dropRect{mouseX, mouseY, 150.0f, divHeight};

    SDL_SetRenderDrawColor(renderer, 185, 181, 222, 128);
    SDL_RenderFillRect(renderer, &dropRect);
}

void SongRoll::beginDrop(SDL_DropEvent& d) {
}

void SongRoll::dropFile(SDL_DropEvent& d) {
    AudioClip* e = em->newAudioClip(d.data);
    if (!e) return;

    auto startPairs = pairsAtMouseX();
    auto endPairs = lastPositionDurationPairs.empty()
        ? addVec(startPairs, std::vector<std::pair<int,int>>{{2,1}})
        : addVec(startPairs, lastPositionDurationPairs);
    int trackID = getHoveredTrack();

    auto track = tracks->getTrack(trackID);
    if (track  == nullptr) return;
    if (track->type != TrackType::Audio) return; // cant put audioclip on region track

    project->um->newAction(new CreatePositionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id),
        static_cast<int>(e->id), startPairs, endPairs, static_cast<uint16_t>(trackID)));

    auto* elem = em->getElement(e->id);
    if (elem && !elem->positions.empty()) {
        auto* pos = elem->positions.back();
        lastPositionDurationPairs = subVec(pos->rhythmEndVector, pos->rhythmVector);
    }

    refreshGrid = true;
}

void SongRoll::clearTextures() {
    SDL_DestroyTexture(texture);
    SDL_DestroyTexture(regionTexture);
    SDL_DestroyTexture(playHeadTexture);
    SDL_DestroyTexture(gridTexture);

    texture = nullptr;
    regionTexture = nullptr;
    playHeadTexture = nullptr;
    gridTexture = nullptr;

    em->clearTextures();
}

void SongRoll::generateTextures(SDL_Renderer* renderer) {
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    regionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    playHeadTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
}

void SongRoll::createPianoRoll(Region* region, bool createUndo, int forceEwID) {
    if (!project || !project->processor) return;

    auto* wm = project->processor->getWindowManager();
    if (!wm) return;

    auto prw = std::make_unique<PianoRollWindow>(region, this);
    PianoRollWindow* ptr = prw.get();

    ExpandedWindow* ew = wm->addWindow(std::move(prw), 800, 600, "Piano Roll");
    if (!ew) return;

    ew->show();
    pianoRollWindows.push_back(ptr);

    pianoRolls.push_back(ptr->getPianoRoll());

    if (createUndo && project->um && parentNode) {
        auto* action = new TogglePianoRollWindowAction(project,
            parentNode->nm->managerPath, static_cast<int>(parentNode->id),
            static_cast<int>(region->id), ew->id,
            0.f, 0.f, 800.f, 600.f, 0, true);
        project->um->newAction(action);
    }
}
