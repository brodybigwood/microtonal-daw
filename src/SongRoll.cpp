#include "SongRoll.h"
#include <algorithm>
#include <cmath>
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
#include "Preferences.h"
#include "UndoManager.h"
#include "ContextMenu.h"
#include "TreeEntry.h"
#include "nodes/arranger/arranger.h"
#include "NodeManager.h"
#include "Note.h"

namespace {
constexpr float kTimelinePosBorderH = 4.0f;
constexpr float kTimelinePosBorderV = 4.0f;
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

    playHead->render(renderer, dW, scrollX);

    renderMargins(renderer);
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
    generateRhythmLines(rhythmLines, rhythmLineLabels, steps, lower, upper);
    // Also update the legacy `times` vector for grid rendering
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
                movingPosition->rhythmIntegerPairs = rhythmLines[li].integerPairs;
            auto startDelta = subVec(movingPosition->rhythmIntegerPairs, lastPosition.rhythmIntegerPairs);
            movingPosition->startOffsetPairs = addVec(lastPosition.startOffsetPairs, startDelta);
            if (parentNode) {
                movingPosition->rhythmEdoSubdivisionSteps = parentNode->rhythmEdoSubdivisionSteps;
                movingPosition->rhythmEdoLowerVector = parentNode->rhythmEdoLowerVector;
                movingPosition->rhythmEdoUpperVector = parentNode->rhythmEdoUpperVector;
            }
            break;
        }
        case PositionDragKind::ResizeRight: {
            float startSec = Note::secondsFromIntegerPairs(lastPosition.rhythmIntegerPairs);
            float newEndSec = Note::secondsFromIntegerPairs(lastPosition.rhythmEndIntegerPairs) + deltaSec;
            const float minLenSec = 1.0f / static_cast<float>(std::max(1, parentNode ? parentNode->rhythmEdoSubdivisionSteps : 1));
            if (newEndSec - startSec < minLenSec)
                newEndSec = startSec + minLenSec;
            size_t li = closestRhythmLineIndexForSeconds(rhythmLines, newEndSec);
            if (li != SIZE_MAX && li < rhythmLines.size())
                movingPosition->rhythmEndIntegerPairs = rhythmLines[li].integerPairs;
            movingPosition->rhythmIntegerPairs = lastPosition.rhythmIntegerPairs;
            if (parentNode) {
                movingPosition->rhythmEdoSubdivisionSteps = parentNode->rhythmEdoSubdivisionSteps;
                movingPosition->rhythmEdoLowerVector = parentNode->rhythmEdoLowerVector;
                movingPosition->rhythmEdoUpperVector = parentNode->rhythmEdoUpperVector;
            }
            break;
        }
        case PositionDragKind::Move:
        default: {
            float startSec = Note::secondsFromIntegerPairs(lastPosition.rhythmIntegerPairs) + deltaSec;
            size_t sli = closestRhythmLineIndexForSeconds(rhythmLines, startSec);
            if (sli != SIZE_MAX && sli < rhythmLines.size()) {
                auto startDelta = subVec(rhythmLines[sli].integerPairs, lastPosition.rhythmIntegerPairs);
                movingPosition->rhythmIntegerPairs = rhythmLines[sli].integerPairs;
                movingPosition->rhythmEndIntegerPairs = addVec(lastPosition.rhythmEndIntegerPairs, startDelta);
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

            movePosition();
            if (mouseX < rightRect.x) {
                const bool resizing = lmb && (positionDragKind == PositionDragKind::ResizeLeft ||
                                              positionDragKind == PositionDragKind::ResizeRight);
                bool overResize = resizing;
                if (!overResize && hoveredPosition) {
                    const float xL = getX(Note::secondsFromIntegerPairs(hoveredPosition->rhythmIntegerPairs));
                    const float xR = getX(Note::secondsFromIntegerPairs(hoveredPosition->rhythmEndIntegerPairs));
                    const float edge = std::max(kPositionResizeEdgePx, kTimelinePosBorderV + 1.0f);
                    if (mouseX <= xL + edge || mouseX >= xR - edge)
                        overResize = true;
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

        const float startSec = Note::secondsFromIntegerPairs(pos.rhythmIntegerPairs);
        const float endSec = Note::secondsFromIntegerPairs(pos.rhythmEndIntegerPairs);
        float offSec = Note::secondsFromIntegerPairs(pos.startOffsetPairs);
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

        const float midY = y0 + kTimelinePosBorderH;
        const float midH = std::max(0.0f, hPx - 2.0f * kTimelinePosBorderH);
        SDL_SetRenderDrawColor(renderer, 120, 200, 255, 255);
        SDL_FRect vLeft{x0, midY, kTimelinePosBorderV, midH};
        SDL_RenderFillRect(renderer, &vLeft);
        SDL_SetRenderDrawColor(renderer, 255, 190, 140, 255);
        SDL_FRect vRight{x0 + wPx - kTimelinePosBorderV, midY, kTimelinePosBorderV, midH};
        SDL_RenderFillRect(renderer, &vRight);

        SDL_SetRenderDrawColor(renderer, 52, 54, 62, 255);
        SDL_FRect hTop{x0, y0, wPx, kTimelinePosBorderH};
        SDL_FRect hBot{x0, y0 + hPx - kTimelinePosBorderH, wPx, kTimelinePosBorderH};
        SDL_RenderFillRect(renderer, &hTop);
        SDL_RenderFillRect(renderer, &hBot);
    }
}

void SongRoll::getHoveredPosition() {
    for (auto e : em->elements) {
        for(auto position :e->positions) {
            auto& pos = *position;
            uint16_t index = tracks->getIndex(pos.trackID);
            const float startSec = Note::secondsFromIntegerPairs(pos.rhythmIntegerPairs);
            const float endSec = Note::secondsFromIntegerPairs(pos.rhythmEndIntegerPairs);
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

    if(id == -1) {
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
        lastPositionDurationPairs = subVec(pos->rhythmEndIntegerPairs, pos->rhythmIntegerPairs);
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
                lastPositionDurationPairs = subVec(pos->rhythmEndIntegerPairs, pos->rhythmIntegerPairs);
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

                positionDragKind = PositionDragKind::None;
                movingPosition = hoveredPosition;
                timelineDragElementId = -1;
                timelineDragPositionId = -1;
                if (movingPosition) {
                    lastPosition = *movingPosition;
                    timelineDragElementId = static_cast<int>(movingPosition->element->id);
                    timelineDragPositionId = movingPosition->id;
                    const float xL = getX(Note::secondsFromIntegerPairs(movingPosition->rhythmIntegerPairs));
                    const float xR = getX(Note::secondsFromIntegerPairs(movingPosition->rhythmEndIntegerPairs));
                    const float handlePx = std::max(kPositionResizeEdgePx, kTimelinePosBorderV + 1.0f);
                    if (mouseX <= xL + handlePx)
                        positionDragKind = PositionDragKind::ResizeLeft;
                    else if (mouseX >= xR - handlePx)
                        positionDragKind = PositionDragKind::ResizeRight;
                    else
                        positionDragKind = PositionDragKind::Move;
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
                } else if (MouseOn(&rightRect) && em->hoveredElement != -1 && !em->hoverNew) {
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
                                const int steps = std::max(1, std::stoi(text));
                                const float lo = std::min(a, b);
                                const float hi = std::max(a, b);
                                const auto& lowerPairs = (a <= b) ? capStartPairs : capEndPairs;
                                const auto& upperPairs = (a <= b) ? capEndPairs : capStartPairs;
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

                if (movingPosition) {
                    json after = GridElement::positionToJson(*movingPosition);
                    json before = GridElement::positionToJson(lastPosition);
                    if (before != after) {
                        GridElement* el = movingPosition->element;
                        project->um->newAction(new MoveElementPositionAction(project, parentNode->nm->managerPath,
                            static_cast<int>(parentNode->id), static_cast<int>(el->id), movingPosition->id, std::move(before),
                            std::move(after)));
                        lastPositionDurationPairs = subVec(movingPosition->rhythmEndIntegerPairs, movingPosition->rhythmIntegerPairs);
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
        lastPositionDurationPairs = subVec(pos->rhythmEndIntegerPairs, pos->rhythmIntegerPairs);
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
