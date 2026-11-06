#include "SongRoll.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>
#include "GridElement.h"
#include "GridView.h"
#include "Settings.h"
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

// Session-wide arranger position clipboard. Entries reference the shared element by id
// (shared placements), exactly like the piano roll's note clipboard is session-wide.
struct PositionClipEntry {
    int elementID = 0;
    uint16_t trackID = 0;
    std::vector<std::pair<int, int>> rhythmVector;
    std::vector<std::pair<int, int>> rhythmEndVector;
    std::vector<std::pair<int, int>> startOffsetPairs;
    int rhythmEdoSteps = 1;
    std::vector<std::pair<int, int>> rhythmEdoLower;
    std::vector<std::pair<int, int>> rhythmEdoUpper;
};

struct PositionClipboard {
    bool valid = false;
    std::vector<int> managerPath;
    int nodeID = -1;
    std::vector<PositionClipEntry> entries;
    std::vector<std::pair<int, int>> firstStartPairs;
    float firstStartSec = 0.f;
    float lastEndSec = 0.f;
};

PositionClipboard g_positionClip;
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

// Close exactly one piano-roll window (the one whose close button was pressed).
void SongRoll::closePianoRollWindow(PianoRollWindow* prw, bool createUndo) {
    if (!prw) return;
    for (size_t i = 0; i < pianoRollWindows.size(); ++i) {
        if (pianoRollWindows[i] != prw) continue;

        int regionId = -1;
        if (i < pianoRolls.size() && pianoRolls[i] && pianoRolls[i]->region)
            regionId = static_cast<int>(pianoRolls[i]->region->id);

        if (createUndo && regionId >= 0 && project && project->um && parentNode) {
            auto* action = new TogglePianoRollWindowAction(project,
                parentNode->nm->managerPath, static_cast<int>(parentNode->id),
                regionId, 0, 0.f, 0.f, 0.f, 0.f, 0, false);
            project->um->newAction(action);
        }

        pianoRollWindows.erase(pianoRollWindows.begin() + static_cast<ptrdiff_t>(i));
        if (i < pianoRolls.size())
            pianoRolls.erase(pianoRolls.begin() + static_cast<ptrdiff_t>(i));

        if (project && project->processor) {
            if (auto* wm = project->processor->getWindowManager())
                wm->removeWindow(prw);
        }
        return; // one window only
    }
}

void SongRoll::validateTimelinePointers() {
    // Drop selected ids whose positions no longer exist (deleted/undone elements).
    if (!selectedPositionIds.empty()) {
        std::set<int> live;
        for (auto* e : em->elements)
            for (auto* p : e->positions) live.insert(p->id);
        for (auto it = selectedPositionIds.begin(); it != selectedPositionIds.end(); ) {
            if (live.count(*it)) ++it;
            else it = selectedPositionIds.erase(it);
        }
    }

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

void SongRoll::bindNode(ArrangerNode* n) {
    if (!n) return;
    parentNode = n;
    // Project loads replace the node's TrackManager/ElementManager; a SongRoll kept
    // alive across window opens must follow them instead of drawing a stale copy.
    if (tracks != n->tracks) {
        tracks = n->tracks;
        if (tracks) {
            tracks->songRoll = this;
            tracks->parentNode = n;
            tracks->mouseX = &mouseX;
            tracks->mouseY = &mouseY;
            tracks->divHeight = &divHeight;
            tracks->scrollY = &scrollY;
            tracks->setGeometry(&leftRect, renderer);
        }
    }
    em = n->elements;
    if (em) em->dstRect = &rightRect;
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
    {
        float tw = 0, th = 0;
        SDL_GetTextureSize(texture, &tw, &th);
        SDL_FRect local{0.f, 0.f, tw, th};
        background::drawBackground(renderer, bgImage,
                                   Settings::instance().getString("gridBackground", ""),
                                   local, colors.background);
    }
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
    renderGridRhythmButton(renderer);

    if (selectingTransportLines) {
        // The drag is on the transport strip, so the highlight belongs in the strip.
        const float startSec = Note::beatsFromVector(transportLineDragStartPairs);
        const float endSec = Note::beatsFromVector(transportLineDragEndPairs);
        const float x1 = dstRect->x + getX(startSec);
        const float x2 = dstRect->x + getX(endSec);
        SDL_FRect band{std::min(x1, x2), dstRect->y + topMargin - Transport::kTimelineHeight,
                       std::max(1.0f, std::fabs(x2 - x1)), Transport::kTimelineHeight};
        SDL_SetRenderDrawColor(renderer, 70, 150, 235, 90);
        SDL_RenderFillRect(renderer, &band);
        SDL_SetRenderDrawColor(renderer, 70, 150, 235, 200);
        SDL_RenderRect(renderer, &band);
    }

    if (selectingPositionBand) {
        const float x1 = dstRect->x + std::min(bandStartX, bandEndX);
        const float y1 = dstRect->y + std::min(bandStartY, bandEndY);
        const float w = std::fabs(bandEndX - bandStartX);
        const float h = std::fabs(bandEndY - bandStartY);
        SDL_FRect band{x1, y1, w, h};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 100, 150, 255, 40);
        SDL_RenderFillRect(renderer, &band);
        SDL_SetRenderDrawColor(renderer, 100, 150, 255, 180);
        SDL_RenderRect(renderer, &band);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

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
    // The rhythm grid is the project-wide piano-roll grid (shared by every piano roll
    // and every arranger), not per-arranger state.
    const int steps = project ? project->pianoRoll.rhythmEdoSubdivisionSteps : 1;
    static const std::vector<std::pair<int, int>> kOneSec{{1,1}};
    const auto& lower = project ? project->pianoRoll.rhythmEdoLowerVector
                                : std::vector<std::pair<int,int>>{};
    const auto& upper = (project && !project->pianoRoll.rhythmEdoUpperVector.empty())
        ? project->pianoRoll.rhythmEdoUpperVector : kOneSec;
    const float minSec = (static_cast<float>(scrollX) - leftMargin) / static_cast<float>(dW);
    const float maxSec = (static_cast<float>(scrollX) + width - leftMargin) / static_cast<float>(dW);
    if (project && !project->pianoRoll.gridSpans.empty())
        generateTransportLines(rhythmLines, rhythmLineLabels, project->pianoRoll.gridSpans, minSec, maxSec);
    else
        generateRhythmLines(rhythmLines, rhythmLineLabels, steps, lower, upper, minSec, maxSec);
    updateTransportLines();
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
        if (rl.isBoundary) {
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
            if (const RhythmGridLine* line = closestRhythmOrTransportLine(
                    rhythmLines, transportLineCache, secondsFromMouseX()))
                movingPosition->rhythmVector = line->integerPairs;
            auto startDelta = subVec(movingPosition->rhythmVector, lastPosition.rhythmVector);
            movingPosition->startOffsetPairs = addVec(lastPosition.startOffsetPairs, startDelta);
            if (parentNode) {
                movingPosition->rhythmEdoSubdivisionSteps = project->pianoRoll.rhythmEdoSubdivisionSteps;
                movingPosition->rhythmEdoLowerVector = project->pianoRoll.rhythmEdoLowerVector;
                movingPosition->rhythmEdoUpperVector = project->pianoRoll.rhythmEdoUpperVector;
            }
            break;
        }
        case PositionDragKind::ResizeRight: {
            float startSec = Note::beatsFromVector(lastPosition.rhythmVector);
            float newEndSec = Note::beatsFromVector(lastPosition.rhythmEndVector) + deltaSec;
            const float minLenSec = 1.0f / static_cast<float>(std::max(1, project ? project->pianoRoll.rhythmEdoSubdivisionSteps : 1));
            if (newEndSec - startSec < minLenSec)
                newEndSec = startSec + minLenSec;
            if (const RhythmGridLine* line = closestRhythmOrTransportLine(rhythmLines, transportLineCache, newEndSec))
                movingPosition->rhythmEndVector = line->integerPairs;
            movingPosition->rhythmVector = lastPosition.rhythmVector;
            if (parentNode) {
                movingPosition->rhythmEdoSubdivisionSteps = project->pianoRoll.rhythmEdoSubdivisionSteps;
                movingPosition->rhythmEdoLowerVector = project->pianoRoll.rhythmEdoLowerVector;
                movingPosition->rhythmEdoUpperVector = project->pianoRoll.rhythmEdoUpperVector;
            }
            break;
        }
        case PositionDragKind::Move:
        default: {
            float startSec = Note::beatsFromVector(lastPosition.rhythmVector) + deltaSec;
            if (const RhythmGridLine* line = closestRhythmOrTransportLine(rhythmLines, transportLineCache, startSec)) {
                auto startDelta = subVec(line->integerPairs, lastPosition.rhythmVector);
                movingPosition->rhythmVector = line->integerPairs;
                movingPosition->rhythmEndVector = addVec(lastPosition.rhythmEndVector, startDelta);
            }
            if (parentNode) {
                movingPosition->rhythmEdoSubdivisionSteps = project->pianoRoll.rhythmEdoSubdivisionSteps;
                movingPosition->rhythmEdoLowerVector = project->pianoRoll.rhythmEdoLowerVector;
                movingPosition->rhythmEdoUpperVector = project->pianoRoll.rhythmEdoUpperVector;
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

    // Apply the grabbed position's exact rational edit delta to the rest of the
    // selection. Both time and vertical track movement move every selected position
    // by the grabbed one's delta. Snapshots prevent per-frame drift.
    if (movingMultiplePositions) {
        const int grabbedOldIdx = tracks->getIndex(lastPosition.trackID);
        const int grabbedNewIdx = tracks->getIndex(movingPosition->trackID);
        const int trackDelta = grabbedNewIdx - grabbedOldIdx;

        for (const auto& snap : multiMoveSnapshots) {
            if (snap.positionID == movingPosition->id) continue;
            auto* pos = findPosition(snap.elementID, snap.positionID);
            if (!pos) continue;
            if (positionDragKind == PositionDragKind::Move && trackDelta != 0) {
                const int srcIdx = tracks->getIndex(snap.trackID);
                if (srcIdx >= 0) {
                    const int dstIdx = srcIdx + trackDelta;
                    if (dstIdx >= 0 && dstIdx < static_cast<int>(tracks->getTracks().size())) {
                        const int dstID = tracks->getID(dstIdx);
                        auto* srcTrack = tracks->getTrack(snap.trackID);
                        auto* dstTrack = tracks->getTrack(dstID);
                        if (srcTrack && dstTrack && dstTrack->type == srcTrack->type)
                            pos->trackID = static_cast<uint16_t>(dstID);
                    }
                }
            }
            switch (positionDragKind) {
                case PositionDragKind::ResizeLeft: {
                    const auto startDelta = subVec(movingPosition->rhythmVector, lastPosition.rhythmVector);
                    const auto newStart = addVec(snap.rhythmVector, startDelta);
                    if (Note::beatsFromVector(newStart) < Note::beatsFromVector(snap.rhythmEndVector)) {
                        pos->rhythmVector = newStart;
                        pos->startOffsetPairs = addVec(snap.startOffsetPairs, startDelta);
                    }
                    break;
                }
                case PositionDragKind::ResizeRight: {
                    const auto endDelta = subVec(movingPosition->rhythmEndVector, lastPosition.rhythmEndVector);
                    const auto newEnd = addVec(snap.rhythmEndVector, endDelta);
                    if (Note::beatsFromVector(newEnd) > Note::beatsFromVector(snap.rhythmVector))
                        pos->rhythmEndVector = newEnd;
                    break;
                }
                case PositionDragKind::Move:
                default: {
                    const auto delta = subVec(movingPosition->rhythmVector, lastPosition.rhythmVector);
                    pos->rhythmVector = addVec(snap.rhythmVector, delta);
                    pos->rhythmEndVector = addVec(snap.rhythmEndVector, delta);
                    break;
                }
            }
        }
    }
}

void SongRoll::handleCustomInput(SDL_Event& e) {

    em->mouseX = mouseX;
    em->mouseY = mouseY;

    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            getHoveredPosition();
            refreshHoveredRhythmLineIndex();

            if (selectingPositionBand) {
                bandEndX = mouseX;
                bandEndY = mouseY;
                refreshGrid = true;
                break;
            }

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

            if (selectingTransportLines) {
                refreshHoveredTransportLineIndex();
                // End snaps to the nearest line of either set (grid rhythm or transport).
                if (const RhythmGridLine* line = closestRhythmOrTransportLine(
                        rhythmLines, transportLineCache, secondsFromMouseX())) {
                    transportLineDragEndPairs = line->integerPairs;
                    if (transportLineDragEndPairs != transportLineDragStartPairs)
                        transportLineDragMoved = true;
                }
                refreshGrid = true;
                break;
            }

            if (selectingRhythmInterval) {
                refreshHoveredRhythmLineIndex();
                if (const RhythmGridLine* line = closestRhythmOrTransportLine(
                        rhythmLines, transportLineCache, secondsFromMouseX())) {
                    rhythmIntervalEndSec = line->seconds;
                    rhythmDragEndVertexPairs = line->integerPairs;
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
                        if (const RhythmGridLine* line = closestRhythmOrTransportLine(
                                rhythmLines, transportLineCache, secondsFromMouseX())) {
                            auto newVec = addVec(subVec(line->integerPairs, *ce->startVec()), *ce->offsetVec());
                            float newSec = Note::beatsFromVector(newVec);
                            // Don't cross adjacent points
                            if (draggingCurvePointIdx > 0) {
                                float prevSec = Note::beatsFromVector(ac->points[draggingCurvePointIdx - 1].timeVec);
                                if (newSec <= prevSec) newVec = ac->points[draggingCurvePointIdx - 1].timeVec;
                            }
                            if (draggingCurvePointIdx < static_cast<int>(ac->points.size()) - 1) {
                                float nextSec = Note::beatsFromVector(ac->points[draggingCurvePointIdx + 1].timeVec);
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
                    const float xL = getX(Note::beatsFromVector(hoveredPosition->rhythmVector));
                    const float xR = getX(Note::beatsFromVector(hoveredPosition->rhythmEndVector));
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
    if ((mouseX > rightRect.x && mouseX <= (rightRect.x + rightRect.w) &&
         mouseY > rightRect.y && mouseY <= (rightRect.y + rightRect.h)) ||
        em->isDraggingElement()) {
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
        const bool selected = selectedPositionIds.count(pos.id) > 0;
        if (selected) {
            SDL_SetRenderDrawColor(renderer, 60,110,200,160);
        } else if(hoveredPosition == &pos) {
            SDL_SetRenderDrawColor(renderer, 90,90,100,127);
        } else {
            SDL_SetRenderDrawColor(renderer, 20,20,100,127);
        }

        const float startSec = Note::beatsFromVector(pos.rhythmVector);
        const float endSec = Note::beatsFromVector(pos.rhythmEndVector);
        float offSec = Note::beatsFromVector(pos.startOffsetPairs);
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
        if (selected) {
            SDL_SetRenderDrawColor(renderer, 150, 200, 255, 230);
            SDL_FRect selOutline{visRect.x, visRect.y, visRect.w, visRect.h};
            SDL_RenderRect(renderer, &selOutline);
        }

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

        // Element name inside the move bar (same name every position shares).
        const float textW = std::max(0.0f, wPx - 2.0f * kDragBarResizeW - 6.0f);
        if (textW >= 12.0f && !element->name.empty()) {
            SDL_Texture* nameTex = element->nameLabel.get(renderer, element->name, SDL_Color{225, 228, 235, 255});
            if (nameTex) {
                float tw = 0, th = 0;
                SDL_GetTextureSize(nameTex, &tw, &th);
                if (tw > 0 && th > 0) {
                    const float dstH = kDragBarH - 4.0f;
                    SDL_Rect clip{static_cast<int>(x0 + kDragBarResizeW + 2),
                                  static_cast<int>(y0 + 2),
                                  static_cast<int>(textW),
                                  static_cast<int>(kDragBarH)};
                    SDL_SetRenderClipRect(renderer, &clip);
                    SDL_FRect dst{x0 + kDragBarResizeW + 3, y0 + 2, tw * (dstH / th), dstH};
                    SDL_RenderTexture(renderer, nameTex, nullptr, &dst);
                    SDL_SetRenderClipRect(renderer, nullptr);
                }
            }
        }

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
            const float startSec = Note::beatsFromVector(pos.rhythmVector);
            const float endSec = Note::beatsFromVector(pos.rhythmEndVector);
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

GridElement::Position* SongRoll::findPosition(int elementID, int positionID, GridElement** elementOut) {
    if (!em) return nullptr;
    auto it = em->ids.find(static_cast<uint16_t>(elementID));
    if (it == em->ids.end()) return nullptr;
    GridElement* ge = em->elements[it->second];
    if (elementOut) *elementOut = ge;
    for (auto* p : ge->positions)
        if (p->id == positionID) return p;
    return nullptr;
}

void SongRoll::copySelectedPositions() {
    if (!em || !parentNode) return;
    struct Src { GridElement* element; GridElement::Position* position; };
    std::vector<Src> src;
    for (auto* e : em->elements)
        for (auto* p : e->positions)
            if (selectedPositionIds.count(p->id)) src.push_back({e, p});
    if (src.empty() && hoveredPosition)
        src.push_back({hoveredPosition->element, hoveredPosition});
    if (src.empty()) return;

    std::sort(src.begin(), src.end(), [](const Src& a, const Src& b) {
        const float sa = Note::beatsFromVector(a.position->rhythmVector);
        const float sb = Note::beatsFromVector(b.position->rhythmVector);
        if (sa != sb) return sa < sb;
        return a.position->trackID < b.position->trackID;
    });

    PositionClipboard clip;
    clip.valid = true;
    clip.managerPath = parentNode->nm->managerPath;
    clip.nodeID = static_cast<int>(parentNode->id);
    bool first = true;
    for (const auto& s : src) {
        PositionClipEntry e;
        e.elementID = static_cast<int>(s.element->id);
        e.trackID = s.position->trackID;
        e.rhythmVector = s.position->rhythmVector;
        e.rhythmEndVector = s.position->rhythmEndVector;
        e.startOffsetPairs = s.position->startOffsetPairs;
        e.rhythmEdoSteps = s.position->rhythmEdoSubdivisionSteps;
        e.rhythmEdoLower = s.position->rhythmEdoLowerVector;
        e.rhythmEdoUpper = s.position->rhythmEdoUpperVector;
        const float startSec = Note::beatsFromVector(e.rhythmVector);
        const float endSec = Note::beatsFromVector(e.rhythmEndVector);
        if (first) {
            clip.firstStartSec = startSec;
            clip.firstStartPairs = e.rhythmVector;
            clip.lastEndSec = endSec;
            first = false;
        } else {
            if (startSec < clip.firstStartSec) {
                clip.firstStartSec = startSec;
                clip.firstStartPairs = e.rhythmVector;
            }
            clip.lastEndSec = std::max(clip.lastEndSec, endSec);
        }
        clip.entries.push_back(std::move(e));
    }
    g_positionClip = std::move(clip);
}

void SongRoll::pastePositionClipboard(float firstBeat, bool exactDuplicate) {
    if (!em || !parentNode || !project || !project->um) return;
    if (!g_positionClip.valid || g_positionClip.entries.empty()) return;
    if (g_positionClip.managerPath != parentNode->nm->managerPath ||
        g_positionClip.nodeID != static_cast<int>(parentNode->id))
        return;

    std::vector<std::pair<int, int>> shift;
    if (!exactDuplicate) {
        // Exact when the target lands on a rhythm/transport line; otherwise the note
        // paste's rational 1/48-beat quantization.
        constexpr float kLineEpsilon = 1e-4f;
        const auto tryLines = [&](const std::vector<RhythmGridLine>& lines) {
            for (const auto& line : lines)
                if (std::fabs(line.seconds - firstBeat) < kLineEpsilon) {
                    shift = subVec(line.integerPairs, g_positionClip.firstStartPairs);
                    return true;
                }
            return false;
        };
        if (!tryLines(rhythmLines) && !tryLines(transportLineCache)) {
            const float shiftBeats = firstBeat - g_positionClip.firstStartSec;
            if (std::fabs(shiftBeats) > 1e-6f) {
                int den = 48;
                int num = static_cast<int>(std::lround(shiftBeats * static_cast<float>(den)));
                const int g = std::gcd(std::abs(num), den);
                num /= g;
                den /= g;
                if (num != 0) shift = {{num, den}};
            }
        }
    }

    std::vector<CreatePositionSpec> specs;
    for (const auto& e : g_positionClip.entries) {
        auto it = em->ids.find(static_cast<uint16_t>(e.elementID));
        if (it == em->ids.end()) continue;   // element deleted since the copy
        GridElement* ge = em->elements[it->second];
        auto* track = tracks->getTrack(e.trackID);
        if (!track) continue;                // track removed since the copy
        const bool notesTrack = track->getType() == TrackType::Notes;
        if (notesTrack != (ge->type == ElementType::region)) continue;
        CreatePositionSpec s;
        s.elementID = e.elementID;
        s.trackID = e.trackID;
        s.rhythmEdoSteps = e.rhythmEdoSteps;
        s.rhythmEdoLower = e.rhythmEdoLower;
        s.rhythmEdoUpper = e.rhythmEdoUpper;
        s.startOffsetPairs = e.startOffsetPairs;
        s.rhythmVector = shift.empty() ? e.rhythmVector : addVec(e.rhythmVector, shift);
        s.rhythmEndVector = shift.empty() ? e.rhythmEndVector : addVec(e.rhythmEndVector, shift);
        specs.push_back(std::move(s));
    }
    if (specs.empty()) return;

    auto* action = new CreatePositionsAction(project, parentNode->nm->managerPath,
        static_cast<int>(parentNode->id), std::move(specs));
    project->um->newAction(action);
    selectedPositionIds.clear();
    for (const auto& s : action->specs)
        if (s.positionID >= 0) selectedPositionIds.insert(s.positionID);
    refreshGrid = true;
}

void SongRoll::deleteSelectedPositions() {
    if (!em || !parentNode || !project || !project->um) return;
    std::vector<std::pair<int, int>> targets;   // (elementID, positionID)
    std::set<int> clearedRegions;               // regions that lose every position
    for (auto* e : em->elements) {
        size_t selectedHere = 0;
        for (auto* p : e->positions)
            if (selectedPositionIds.count(p->id)) ++selectedHere;
        if (selectedHere == 0) continue;
        if (e->type == ElementType::region && selectedHere == e->positions.size())
            clearedRegions.insert(e->id);
        for (auto* p : e->positions)
            if (selectedPositionIds.count(p->id))
                targets.push_back({e->id, p->id});
    }
    if (targets.empty() && hoveredPosition) {
        targets.push_back({hoveredPosition->element->id, hoveredPosition->id});
        if (hoveredPosition->element->type == ElementType::region &&
            hoveredPosition->element->positions.size() == 1)
            clearedRegions.insert(hoveredPosition->element->id);
    }
    if (targets.empty()) return;

    for (int rid : clearedRegions) clearPianoRoll(rid, false);
    project->um->newAction(new DeletePositionsAction(project, parentNode->nm->managerPath,
        static_cast<int>(parentNode->id), std::move(targets)));
    selectedPositionIds.clear();
    refreshGrid = true;
}

bool SongRoll::handleKeyboard(SDL_Event& e) {
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return false;
    switch (e.key.scancode) {
        case SDL_SCANCODE_C:
            if (isCtrlPressed) copySelectedPositions();
            break;
        case SDL_SCANCODE_V:
            if (isCtrlPressed) pastePositionClipboard(static_cast<float>(project->beatPosition));
            break;
        case SDL_SCANCODE_B:
            if (isCtrlPressed) {
                copySelectedPositions();
                if (g_positionClip.valid) pastePositionClipboard(g_positionClip.lastEndSec);
            }
            break;
        case SDL_SCANCODE_D:
            if (isCtrlPressed) {
                copySelectedPositions();
                if (g_positionClip.valid) pastePositionClipboard(g_positionClip.firstStartSec, true);
            }
            break;
        case SDL_SCANCODE_DELETE:
        case SDL_SCANCODE_BACKSPACE:
            deleteSelectedPositions();
            break;
        default:
            return false;
    }
    return true;
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
    // Only double-clicking an existing region position opens its piano roll; the
    // old double-click region creation is gone.
    if (!hoveredPosition) return;
    auto e = hoveredPosition->element;
    if (!e || e->type != ElementType::region) return;
    auto reg = static_cast<Region*>(e);
    movingPosition = nullptr;
    positionDragKind = PositionDragKind::None;
    lmb = false;
    createPianoRoll(reg, false);
}

bool SongRoll::positionGrabBarHit(const GridElement::Position* pos) {
    if (!pos || !tracks) return false;
    const int index = tracks->getIndex(pos->trackID);
    if (index < 0) return false;
    const float yT = getY(static_cast<float>(index));
    if (mouseY < yT || mouseY > yT + kDragBarH) return false;
    const float xL = getX(Note::beatsFromVector(pos->rhythmVector));
    const float xR = getX(Note::beatsFromVector(pos->rhythmEndVector));
    if (xR - xL <= 2.0f * kDragBarResizeW) return false;
    return mouseX >= xL + kDragBarResizeW && mouseX <= xR - kDragBarResizeW;
}

void SongRoll::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:

            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                last_lmb_x = mouseX;
                last_lmb_y = mouseY;
                // Hover is normally refreshed on motion; a click after a scroll would
                // otherwise act on a stale position.
                getHoveredPosition();

                if (mouseY < topMargin && mouseX < leftMargin) {
                    if (recordButtonHit(mouseX, mouseY)) {
                        project->setRecording(!project->isRecording);
                        return;
                    }
                    if (gridRhythmButtonHit(mouseX, mouseY)) {
                        applyTransportToGrid();
                        return;
                    }
                }

                // Ctrl on the transport strip: drag between two transport lines.
                // Ctrl+Shift uses the same drag to rewrite the neighbouring lines instead.
                {
                    const float stripTop = topMargin - Transport::kTimelineHeight;
                    if (isCtrlPressed && mouseY >= stripTop && mouseY <= topMargin &&
                        mouseX > leftMargin && mouseX < rightRect.x) {
                        updateTransportLines();
                        refreshHoveredTransportLineIndex();
                        selectingTransportLines = true;
                        transportLineDragExtend = isShiftPressed;
                        transportLineDragMoved = false;
                        // Start may sit on either the transport strip or the grid rhythm.
                        if (const RhythmGridLine* line = closestRhythmOrTransportLine(
                                rhythmLines, transportLineCache, secondsFromMouseX()))
                            transportLineDragStartPairs = line->integerPairs;
                        else
                            transportLineDragStartPairs.clear();
                        transportLineDragEndPairs = transportLineDragStartPairs;
                        return;
                    }
                }

                // Ctrl+Shift: start rhythm interval drag
                if (isCtrlPressed && isShiftPressed && mouseX > leftMargin && mouseX < rightRect.x) {
                    rhythmEdoDefineDialogOpen = false;
                    selectingRhythmInterval = true;
                    refreshHoveredRhythmLineIndex();
                    if (const RhythmGridLine* line = closestRhythmOrTransportLine(
                            rhythmLines, transportLineCache, secondsFromMouseX())) {
                        rhythmIntervalStartSec = line->seconds;
                        rhythmDragStartVertexPairs = line->integerPairs;
                    } else {
                        rhythmIntervalStartSec = 0.0f;
                        rhythmDragStartVertexPairs.clear();
                    }
                    rhythmIntervalEndSec = rhythmIntervalStartSec;
                    rhythmDragEndVertexPairs.clear();
                    rhythmIntervalDragMoved = false;
                    return;
                }

                // Ctrl (without Shift): toggle a position in the selection, or start a
                // rubber band on empty grid (mirrors the piano roll's Ctrl+click notes).
                if (isCtrlPressed && !isShiftPressed && mouseX > gridRect.x &&
                    mouseX < gridRect.x + gridRect.w && mouseY > gridRect.y &&
                    mouseY < gridRect.y + gridRect.h) {
                    if (hoveredPosition) {
                        const int pid = hoveredPosition->id;
                        if (!selectedPositionIds.insert(pid).second)
                            selectedPositionIds.erase(pid);
                    } else {
                        selectingPositionBand = true;
                        bandStartX = bandEndX = mouseX;
                        bandStartY = bandEndY = mouseY;
                    }
                    refreshGrid = true;
                    return;
                }

                // Curve point interaction for automation positions (below drag bar only)
                {
                    GridElement::Position* curvePos = hoveredPosition;
                    if (!curvePos || curvePos->element->type != ElementType::automationCurve) {
                        for (auto* e : em->elements) {
                            if (e->type != ElementType::automationCurve) continue;
                            for (auto* p : e->positions) {
                                float xL = getX(Note::beatsFromVector(p->rhythmVector));
                                float xR = getX(Note::beatsFromVector(p->rhythmEndVector));
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

                // Plain click: select the clicked position (keep a multi-selection it is
                // already part of), or clear the selection on empty grid.
                if (hoveredPosition) {
                    if (!selectedPositionIds.count(hoveredPosition->id)) {
                        selectedPositionIds.clear();
                        selectedPositionIds.insert(hoveredPosition->id);
                    }
                } else if (!isCtrlPressed) {
                    selectedPositionIds.clear();
                }

                positionDragKind = PositionDragKind::None;
                movingPosition = hoveredPosition;
                movingMultiplePositions = false;
                multiMoveSnapshots.clear();
                timelineDragElementId = -1;
                timelineDragPositionId = -1;
                if (movingPosition) {
                    const float xL = getX(Note::beatsFromVector(movingPosition->rhythmVector));
                    const float xR = getX(Note::beatsFromVector(movingPosition->rhythmEndVector));
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
                    // New positions take the shape of the last one touched by a click.
                    lastPositionDurationPairs = subVec(movingPosition->rhythmEndVector, movingPosition->rhythmVector);
                    lastPositionStartOffsetPairs = movingPosition->startOffsetPairs;
                    timelineDragElementId = static_cast<int>(movingPosition->element->id);
                    timelineDragPositionId = movingPosition->id;

                    // Dragging a member of a multi-selection applies the same edit to the
                    // whole selection (move, or the grabbed edge's resize delta). The
                    // grabbed position alone may also change track.
                    if (positionDragKind != PositionDragKind::None &&
                        selectedPositionIds.size() > 1 &&
                        selectedPositionIds.count(movingPosition->id)) {
                        movingMultiplePositions = true;
                        for (auto* el : em->elements)
                            for (auto* p : el->positions) {
                                if (!selectedPositionIds.count(p->id)) continue;
                                PositionDragSnapshot snap;
                                snap.elementID = static_cast<int>(el->id);
                                snap.positionID = p->id;
                                snap.trackID = p->trackID;
                                snap.rhythmVector = p->rhythmVector;
                                snap.rhythmEndVector = p->rhythmEndVector;
                                snap.startOffsetPairs = p->startOffsetPairs;
                                snap.before = GridElement::positionToJson(*p);
                                multiMoveSnapshots.push_back(std::move(snap));
                            }
                    }
                }

                const bool withinDct = SDL_GetTicks() - lastLmbTime < DCT;
                const bool regionPosition = hoveredPosition && hoveredPosition->element &&
                    hoveredPosition->element->type == ElementType::region;
                if (withinDct && regionPosition) doubleClick();
                else if (!hoveredPosition &&
                    mouseX > gridRect.x && mouseX < gridRect.x + gridRect.w &&
                    mouseY > gridRect.y && mouseY < gridRect.y + gridRect.h) {
                    createElement();
                }

                lastLmbTime = SDL_GetTicks();
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                // Ctrl+Shift+right-click a position: recall its rhythm (mirrors the piano
                // roll's Ctrl+Shift+right-click on a note).
                if (isCtrlPressed && isShiftPressed && hoveredPosition && mouseX < rightRect.x) {
                    recallPositionRhythm(hoveredPosition);
                    rmb = false;
                    return;
                }
                // Curve point deletion
                if (hoveredCurvePointIdx >= 0 && hoveredPosition &&
                    hoveredPosition->element->type == ElementType::automationCurve) {
                    auto* ac = static_cast<AutomationCurve*>(hoveredPosition->element);
                    int ptIdx = hoveredCurvePointIdx;
                    auto* ce = getCurveEditor(hoveredPosition);
                    auto beforeSnapshot = std::make_shared<json>();
                    auto menuRoot = ce->buildPointMenu(ptIdx,
                        [this, ac, beforeSnapshot]() { *beforeSnapshot = snapshotCurvePoints(ac); },
                        [this, ac, beforeSnapshot]() { pushCurveUndo(ac, *beforeSnapshot); refreshGrid = true; });
                    auto ctxMenu = ContextMenu::get();
                    ctxMenu->skipNextEvent = true;
                    ctxMenu->activate();
                    ctxMenu->dynamicTick = getTreeMenuTicker(menuRoot);
                    rmb = false;
                    return;
                }
                if (hoveredPosition && mouseX < rightRect.x && positionGrabBarHit(hoveredPosition)) {
                    // Position menu, only from the move bar (not the body or handles).
                    GridElement* el = hoveredPosition->element;
                    const int elemID = static_cast<int>(el->id);
                    const int posID = hoveredPosition->id;
                    auto root = uTreeEntry();
                    root->label = el->name;

                    if (el->type == ElementType::region) {
                        auto open = uTreeEntry();
                        open->label = "Open Piano Roll";
                        open->click = [this, elemID]() {
                            auto* reg = static_cast<Region*>(em->getElement(static_cast<uint16_t>(elemID)));
                            if (reg) createPianoRoll(reg, true);
                        };
                        root->addChild(open);
                    }
                    auto selectAll = uTreeEntry();
                    selectAll->label = "Select All Positions of This Element";
                    selectAll->click = [this, elemID]() {
                        selectedPositionIds.clear();
                        for (auto* e : em->elements)
                            if (static_cast<int>(e->id) == elemID)
                                for (auto* p : e->positions) selectedPositionIds.insert(p->id);
                        refreshGrid = true;
                    };
                    root->addChild(selectAll);
                    auto delPos = uTreeEntry();
                    delPos->label = "Delete Position";
                    delPos->click = [this, elemID, posID]() {
                        project->um->newAction(new DeletePositionAction(project, parentNode->nm->managerPath,
                            static_cast<int>(parentNode->id), elemID, posID));
                        refreshGrid = true;
                    };
                    root->addChild(delPos);
                    if (selectedPositionIds.count(posID) && selectedPositionIds.size() > 1) {
                        auto delSel = uTreeEntry();
                        delSel->label = "Delete Selection (" + std::to_string(selectedPositionIds.size()) + ")";
                        delSel->click = [this]() { deleteSelectedPositions(); };
                        root->addChild(delSel);
                    }
                    auto ctxMenu = ContextMenu::get();
                    ctxMenu->skipNextEvent = true;
                    ctxMenu->activate();
                    ctxMenu->dynamicTick = getTreeMenuTicker(root);
                    rmb = false;
                    return;
                }
                if (hoveredPosition && mouseX < rightRect.x) {
                    // A right-click on a selected position deletes the whole selection.
                    if (selectedPositionIds.count(hoveredPosition->id)) {
                        deleteSelectedPositions();
                        return;
                    }
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
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                // Rubber-band selection: add every intersecting position.
                if (selectingPositionBand) {
                    selectingPositionBand = false;
                    const float bx1 = std::min(bandStartX, bandEndX);
                    const float bx2 = std::max(bandStartX, bandEndX);
                    const float by1 = std::min(bandStartY, bandEndY);
                    const float by2 = std::max(bandStartY, bandEndY);
                    for (auto* el : em->elements)
                        for (auto* p : el->positions) {
                            const int idx = tracks->getIndex(p->trackID);
                            if (idx < 0) continue;
                            const float x1 = getX(Note::beatsFromVector(p->rhythmVector));
                            const float x2 = getX(Note::beatsFromVector(p->rhythmEndVector));
                            const float y1 = getY(idx);
                            const float y2 = getY(idx + 1);
                            if (x2 >= bx1 && x1 <= bx2 && y2 >= by1 && y1 <= by2)
                                selectedPositionIds.insert(p->id);
                        }
                    lmb = false;
                    refreshGrid = true;
                    break;
                }

                // Ctrl+drag on the strip: commit or prompt.
                if (selectingTransportLines) {
                    selectingTransportLines = false;
                    if (transportLineDragExtend) {
                        if (transportLineDragMoved)
                            extendTransportLines(transportLineDragEndPairs);
                    } else if (transportLineDragMoved) {
                        const auto aPairs = transportLineDragStartPairs;
                        const auto bPairs = transportLineDragEndPairs;
                        auto ctxMenu = ContextMenu::get();
                        ctxMenu->skipNextEvent = true;
                        if (project && project->window)
                            SDL_StartTextInput(project->window);
                        ctxMenu->activate();
                        ctxMenu->dynamicTick = getTextInputTicker(
                            [this, aPairs, bPairs](std::string text) {
                                try {
                                    subdivideBetweenRhythmLines(aPairs, bPairs, std::max(1, std::stoi(text)));
                                } catch (...) {}
                            },
                            []() {});
                    }
                    refreshGrid = true;
                }

                // Ctrl+Shift: finish rhythm interval drag
                if (selectingRhythmInterval) {
                    selectingRhythmInterval = false;
                    rhythmEdoDefineDialogOpen = false;
                    if (std::fabs(rhythmIntervalEndSec - rhythmIntervalStartSec) < 0.001f) {
                        // Ctrl+Shift+click on a position without dragging: recall its rhythm.
                        recallPositionRhythm(hoveredPosition);
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
                                const float otherSec = Note::beatsFromVector(otherPairs);
                                const auto& lowerPairs = (fixedSec <= otherSec) ? capStartPairs : otherPairs;
                                const auto& upperPairs = (fixedSec <= otherSec) ? otherPairs : capStartPairs;
                                int steps = num;
                                const json before = arrangerRhythmEdoJson();
                                project->pianoRoll.rhythmEdoSubdivisionSteps = steps;
                                project->pianoRoll.rhythmEdoLowerVector = lowerPairs;
                                project->pianoRoll.rhythmEdoUpperVector = upperPairs;
                                RhythmLatticeSpan gridSpan;
                                gridSpan.steps = steps;
                                gridSpan.lower = lowerPairs;
                                gridSpan.upper = upperPairs;
                                project->pianoRoll.gridSpans.assign(1, gridSpan);
                                commitArrangerRhythmEdo(before, "Define Rhythm");
                                refreshGrid = true;
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
                    if (movingMultiplePositions) {
                        // One undo step for the whole group.
                        std::vector<MovePositionSpec> specs;
                        for (const auto& snap : multiMoveSnapshots) {
                            auto* pos = findPosition(snap.elementID, snap.positionID);
                            if (!pos) continue;
                            json after = GridElement::positionToJson(*pos);
                            if (after == snap.before) continue;
                            MovePositionSpec spec;
                            spec.elementID = snap.elementID;
                            spec.positionID = snap.positionID;
                            spec.before = snap.before;
                            spec.after = std::move(after);
                            specs.push_back(std::move(spec));
                        }
                        if (!specs.empty()) {
                            auto* act = new MoveElementPositionsAction(project, parentNode->nm->managerPath,
                                static_cast<int>(parentNode->id), std::move(specs));
                            project->um->newAction(act);
                            refreshGrid = true;
                        }
                    } else {
                        json after = GridElement::positionToJson(*movingPosition);
                        json before = GridElement::positionToJson(lastPosition);
                        if (before != after) {
                            GridElement* el = movingPosition->element;
                            project->um->newAction(new MoveElementPositionAction(project, parentNode->nm->managerPath,
                                static_cast<int>(parentNode->id), static_cast<int>(el->id), movingPosition->id, std::move(before),
                                std::move(after)));
                            if (el)
                                em->currentElement = static_cast<int>(el->id);
                            refreshGrid = true;
                        }
                    }
                    lastPositionDurationPairs = subVec(movingPosition->rhythmEndVector, movingPosition->rhythmVector);
                    lastPositionStartOffsetPairs = movingPosition->startOffsetPairs;
                }
                lmb = false;
                movingPosition = nullptr;
                movingMultiplePositions = false;
                multiMoveSnapshots.clear();
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
    auto* action = new CreateAudioClipAction(project, parentNode->nm->managerPath,
        static_cast<int>(parentNode->id), d.data);
    project->um->newAction(action);
    if (!action->snapshotValid) return;

    auto startPairs = pairsAtMouseX();
    auto endPairs = lastPositionDurationPairs.empty()
        ? addVec(startPairs, std::vector<std::pair<int,int>>{{2,1}})
        : addVec(startPairs, lastPositionDurationPairs);
    int trackID = getHoveredTrack();

    auto track = tracks->getTrack(trackID);
    if (track  == nullptr) return;
    if (track->type != TrackType::Audio) return; // cant put audioclip on region track

    project->um->newAction(new CreatePositionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id),
        action->clipID, startPairs, endPairs, static_cast<uint16_t>(trackID)));

    auto* elem = em->getElement(static_cast<uint16_t>(action->clipID));
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

    // The WM close button closes exactly this window (and only this window).
    ew->onRequestClose = [this, ptr]() {
        closePianoRollWindow(ptr, /*createUndo=*/true);
    };

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

// ---------------------------------------------------------------------------
// Ctrl+drag on the transport strip. The strip's lines are the arranger's
// transportLines; press on one line, release on another, then type how many
// parts to insert between them. Only those two lines are subdivided, so the
// surrounding lines keep their spacing. Ctrl+Shift takes the spacing between
// the two nearest lines on one side and writes it into the other direction,
// overwriting the lines already there.
// ---------------------------------------------------------------------------

json SongRoll::arrangerRhythmEdoJson() const {
    json j;
    j["steps"] = project ? project->pianoRoll.rhythmEdoSubdivisionSteps : 1;
    j["lower"] = json::array();
    if (project)
        for (const auto& pr : project->pianoRoll.rhythmEdoLowerVector)
            j["lower"].push_back(json::array({pr.first, pr.second}));
    j["upper"] = json::array();
    if (project)
        for (const auto& pr : project->pianoRoll.rhythmEdoUpperVector)
            j["upper"].push_back(json::array({pr.first, pr.second}));
    // The transport strip is per-arranger; only the grid itself is project-wide.
    j["transportLines"] = latticeSpansToJSON(parentNode ? parentNode->transportLines
                                                        : std::vector<RhythmLatticeSpan>{});
    j["gridSpans"] = latticeSpansToJSON(project ? project->pianoRoll.gridSpans
                                                : std::vector<RhythmLatticeSpan>{});
    return j;
}

void SongRoll::commitArrangerRhythmEdo(const json& before, const std::string& label) {
    if (!parentNode || !project || !project->um) return;
    updateRhythmLines();
    auto* action = new SongRollRhythmEdoAction(project, parentNode->nm->managerPath,
        static_cast<int>(parentNode->id), before, arrangerRhythmEdoJson());
    action->name = label;
    project->um->newAction(action);
}

void SongRoll::recallPositionRhythm(const GridElement::Position* pos) {
    if (!pos || !parentNode || !project || !project->um) return;
    // Snapshot through the canonical JSON so gridSpans (not just the EDO triple)
    // round-trips through the undo action.
    const json before = arrangerRhythmEdoJson();
    project->pianoRoll.rhythmEdoSubdivisionSteps = pos->rhythmEdoSubdivisionSteps;
    project->pianoRoll.rhythmEdoLowerVector = pos->rhythmEdoLowerVector;
    project->pianoRoll.rhythmEdoUpperVector = pos->rhythmEdoUpperVector;
    RhythmLatticeSpan span;
    span.steps = pos->rhythmEdoSubdivisionSteps;
    span.lower = pos->rhythmEdoLowerVector;
    span.upper = pos->rhythmEdoUpperVector;
    project->pianoRoll.gridSpans.assign(1, span);
    commitArrangerRhythmEdo(before, "Recall Position Rhythm");
    refreshGrid = true;
}

void SongRoll::updateTransportLines() {
    transportLineCache.clear();
    if (!parentNode) return;

    if (parentNode->transportLines.empty())
        parentNode->transportLines.push_back(defaultLatticeSpan(1));

    const float minSec = (static_cast<float>(scrollX) - leftMargin) / static_cast<float>(dW);
    const float maxSec = (static_cast<float>(scrollX) + width - leftMargin) / static_cast<float>(dW);
    std::vector<std::string> labels;
    generateTransportLines(transportLineCache, labels, parentNode->transportLines, minSec, maxSec);
}

size_t SongRoll::closestTransportLineIndexForSeconds(float seconds) {
    return ::closestRhythmLineIndexForSeconds(transportLineCache, seconds);
}

void SongRoll::refreshHoveredTransportLineIndex() {
    hoveredTransportLineIndex = SIZE_MAX;
    if (transportLineCache.empty()) return;
    if (mouseX < leftMargin) return;
    hoveredTransportLineIndex = closestTransportLineIndexForSeconds(secondsFromMouseX());
}

void SongRoll::applyTransportToGrid() {
    if (!project || !parentNode || parentNode->transportLines.empty()) return;
    if (parentNode->transportLines == project->pianoRoll.gridSpans) return;
    const json before = arrangerRhythmEdoJson();
    // Copy this arranger's transport strip into the shared grid; the strip itself stays
    // per-arranger.
    project->pianoRoll.gridSpans = parentNode->transportLines;
    commitArrangerRhythmEdo(before, "Grid From Transport");
}

void SongRoll::subdivideBetweenRhythmLines(const std::vector<std::pair<int, int>>& aPairs,
                                           const std::vector<std::pair<int, int>>& bPairs,
                                           int parts) {
    if (!project || !parentNode || parts < 2) return;

    const bool aFirst = Note::beatsFromVector(aPairs) <= Note::beatsFromVector(bPairs);
    RhythmLatticeSpan span;
    span.steps = parts;
    span.lower = aFirst ? aPairs : bPairs;
    span.upper = aFirst ? bPairs : aPairs;

    const json before = arrangerRhythmEdoJson();
    insertLatticeSpan(parentNode->transportLines, span);

    if (arrangerRhythmEdoJson() == before) return;
    commitArrangerRhythmEdo(before, "Subdivide Rhythm Lines");
}

void SongRoll::extendTransportLines(const std::vector<std::pair<int, int>>& cursorPairs) {
    if (!project || !parentNode) return;
    const float cursorSec = Note::beatsFromVector(cursorPairs);

    // Spacing comes from the span the cursor sits in.
    int sourceSteps = std::max(1, project->pianoRoll.rhythmEdoSubdivisionSteps);
    for (const auto& s : parentNode->transportLines) {
        if (Note::beatsFromVector(s.lower) <= cursorSec && cursorSec <= Note::beatsFromVector(s.upper)) {
            sourceSteps = s.steps;
            break;
        }
    }

    // Write that spacing into the larger neighbouring interval, up to the beat line.
    const RhythmGridLine* prevBeat = nullptr;
    const RhythmGridLine* nextBeat = nullptr;
    for (const auto& rl : rhythmLines) {
        if (rl.seconds < cursorSec && (!prevBeat || rl.seconds > prevBeat->seconds)) prevBeat = &rl;
        if (rl.seconds > cursorSec && (!nextBeat || rl.seconds < nextBeat->seconds)) nextBeat = &rl;
    }
    const float leftSpan = prevBeat ? cursorSec - prevBeat->seconds : -1.f;
    const float rightSpan = nextBeat ? nextBeat->seconds - cursorSec : -1.f;
    const bool useLeft = (leftSpan >= rightSpan);
    if (useLeft ? !prevBeat : !nextBeat) return;

    RhythmLatticeSpan span;
    span.steps = sourceSteps;
    span.lower = useLeft ? prevBeat->integerPairs : cursorPairs;
    span.upper = useLeft ? cursorPairs : nextBeat->integerPairs;

    const json before = arrangerRhythmEdoJson();
    insertLatticeSpan(parentNode->transportLines, span);

    if (arrangerRhythmEdoJson() == before) return;
    commitArrangerRhythmEdo(before, "Extend Rhythm Lines");
}
