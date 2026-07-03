#include "SongRoll.h"
#include <algorithm>
#include <cmath>
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

namespace {
constexpr float kTimelinePosBorderH = 4.0f;
constexpr float kTimelinePosBorderV = 4.0f;
/** Half-width of resize grab zone from each vertical edge (screen px). */
constexpr float kPositionResizeEdgePx = 5.0f;

fract nearestBeatFromScreenX(float screenX, float scrollX, float leftMargin, double dW, double notesPerBar) {
    const int gridDen = static_cast<int>(std::max(1.0, std::round(notesPerBar)));
    const double cellPx = dW / notesPerBar;
    const int cell = static_cast<int>(std::lround((static_cast<double>(screenX) + static_cast<double>(scrollX) - static_cast<double>(leftMargin)) / cellPx));
    return fract(cell, gridDen);
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
            // Remove from expanded window manager.
            if (i < pianoRollWindows.size() && project && project->processor) {
                auto* wm = project->processor->getWindowManager();
                // Erase from our list FIRST, then destroy — renderAll may run before next frame.
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

    float x = -1000; //for now only this many measures
    times.clear();
    while(x < 1000) {
        times.push_back(x);
        x++;
    }
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

    if(project->processing) {
        playHead->render(renderer, dW, scrollX);
    }

    renderMargins(renderer);
    return true;
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

void SongRoll::renderMargins(SDL_Renderer* renderer) {
    tracks->render(renderer);
    transport->render(renderer);
    em->render(renderer);
}


SongRoll::~SongRoll() {
    // Destroy expanded windows first.
    if (project && project->processor) {
        auto* wm = project->processor->getWindowManager();
        if (wm) {
            for (auto* pw : pianoRollWindows) {
                wm->removeWindow(pw);
            }
        }
    }
    pianoRollWindows.clear();
    pianoRolls.clear();
}

void SongRoll::movePosition() {
    if (!movingPosition) return;

    const int gridDen = static_cast<int>(std::max(1.0, std::round(notesPerBar)));
    const fract minLen(1, gridDen);
    const fract oldPos = nearestBeatFromScreenX(last_lmb_x, static_cast<float>(scrollX), leftMargin, dW, notesPerBar);
    const fract newPos = nearestBeatFromScreenX(mouseX, static_cast<float>(scrollX), leftMargin, dW, notesPerBar);
    const fract change = newPos - oldPos;

    switch (positionDragKind) {
        case PositionDragKind::ResizeLeft: {
            // Move timeline `start` right while keeping `end` fixed; advance `startOffset` so media trims from
            // the left (regions and audio clips).
            fract newStart = lastPosition.start + change;
            fract newLen = lastPosition.end - newStart;
            if ((double)newLen < (double)minLen) {
                newLen = minLen;
                newStart = lastPosition.end - newLen;
            }
            movingPosition->start = newStart;
            movingPosition->length = newLen;
            movingPosition->end = newStart + newLen;
            const fract trim = newStart - lastPosition.start;
            movingPosition->startOffset = lastPosition.startOffset + trim;
            break;
        }
        case PositionDragKind::ResizeRight: {
            fract newEnd = lastPosition.end + change;
            fract newLen = newEnd - lastPosition.start;
            if ((double)newLen < (double)minLen) {
                newLen = minLen;
                newEnd = lastPosition.start + newLen;
            }
            movingPosition->start = lastPosition.start;
            movingPosition->length = newLen;
            movingPosition->end = newEnd;
            break;
        }
        case PositionDragKind::Move:
        default:
            movingPosition->start = lastPosition.start + change;
            movingPosition->end = lastPosition.end + change;
            movingPosition->length = lastPosition.length;
            break;
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
            movePosition();
            if (mouseX < rightRect.x) {
                const bool resizing = lmb && (positionDragKind == PositionDragKind::ResizeLeft ||
                                              positionDragKind == PositionDragKind::ResizeRight);
                bool overResize = resizing;
                if (!overResize && hoveredPosition) {
                    const float xL = getX(static_cast<float>(static_cast<double>(hoveredPosition->start)));
                    const float xR = getX(static_cast<float>(static_cast<double>(hoveredPosition->end)));
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
    float tempo = project->tempo; // notes per minute
    float barsPerSecond = tempo / (60 * notesPerBar);
    float pixelsPerSecond = dW * barsPerSecond;
    element->draw(renderer, pixelsPerSecond, (int)divHeight);
    SDL_SetRenderTarget(renderer, regionTexture);
    for(auto position : element->positions) {
        auto& pos = *position;
        if(hoveredPosition == &pos) {
            SDL_SetRenderDrawColor(renderer, 90,90,100,127);
        } else {
            SDL_SetRenderDrawColor(renderer, 20,20,100,127);
        }

        float topLeftCornerX = getX(pos.start);
        uint16_t index = tracks->getIndex(pos.trackID);
        float topLeftCornerY = getY(index);
        SDL_FRect dstRectE = {topLeftCornerX, topLeftCornerY, (float)pos.length*dW, divHeight};

        SDL_FRect srcRect;
        switch (element->type) {
            case ElementType::region:
                srcRect = {(float)pos.startOffset * 100, 0, (float)pos.length * 100, 100};
                break;
            case ElementType::audioClip:
                srcRect = {0, 0, dstRectE.w, dstRectE.h};
                break;
            default:
                break;
        }
        SDL_RenderFillRect(renderer, &dstRectE);
        SDL_RenderTexture(renderer, element->texture, &srcRect, &dstRectE);

        const float wPx = (float)pos.length * dW;
        const float hPx = divHeight;
        const float x0 = topLeftCornerX;
        const float y0 = topLeftCornerY;

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
            if(
                mouseX < rightRect.x && 
                mouseX > getX(pos.start) &&
                mouseX < getX(pos.end) &&
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
    fract start = getHoveredTime();

    auto trackID = getHoveredTrack();
    auto track = tracks->getTrack(trackID);
    if (!track) return;

    auto elem = em->getElement(id);
    
    if (track->getType() == TrackType::Notes && elem->type != ElementType::region) return;
    if (elem->type == ElementType::region && track->getType() != TrackType::Notes) return;

    project->um->newAction(new CreatePositionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id),
        static_cast<int>(id), start, static_cast<uint16_t>(trackID)));

    refreshGrid = true;
}

void SongRoll::doubleClick() {
    if (hoveredPosition) {
        auto e = hoveredPosition->element;
        if (e->type == ElementType::region) {
            auto reg = static_cast<Region*>(e);
            // Stop dragging before opening the piano roll.
            movingPosition = nullptr;
            positionDragKind = PositionDragKind::None;
            lmb = false;
            createPianoRoll(reg, false);
        }
    } else {
        auto trackID = getHoveredTrack();
        auto track = tracks->getTrack(trackID);
        if (track && track->type == TrackType::Notes) {
            fract start = getHoveredTime();
            auto* cra = new CreateRegionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id));
            project->um->newAction(cra);
            project->um->newAction(new CreatePositionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id),
                cra->regionID, start, static_cast<uint16_t>(trackID)));
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

                positionDragKind = PositionDragKind::None;
                movingPosition = hoveredPosition;
                timelineDragElementId = -1;
                timelineDragPositionId = -1;
                if (movingPosition) {
                    lastPosition = *movingPosition;
                    timelineDragElementId = static_cast<int>(movingPosition->element->id);
                    timelineDragPositionId = movingPosition->id;
                    const float xL = getX(static_cast<float>(static_cast<double>(movingPosition->start)));
                    const float xR = getX(static_cast<float>(static_cast<double>(movingPosition->end)));
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
                if (movingPosition) {
                    json after = GridElement::positionToJson(*movingPosition);
                    json before = GridElement::positionToJson(lastPosition);
                    if (before != after) {
                        GridElement* el = movingPosition->element;
                        project->um->newAction(new MoveElementPositionAction(project, parentNode->nm->managerPath,
                            static_cast<int>(parentNode->id), static_cast<int>(el->id), movingPosition->id, std::move(before),
                            std::move(after)));
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

    fract start = getHoveredTime();
    int trackID = getHoveredTrack();

    auto track = tracks->getTrack(trackID);
    if (track  == nullptr) return;
    if (track->type != TrackType::Audio) return; // cant put audioclip on region track

    project->um->newAction(new CreatePositionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id),
        static_cast<int>(e->id), start, static_cast<uint16_t>(trackID)));

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

    // Create the SDL3 window — 800x600 default, hidden until shown.
    ExpandedWindow* ew = wm->addWindow(std::move(prw), 800, 600, "Piano Roll");
    if (!ew) return;

    ew->show();
    pianoRollWindows.push_back(ptr);

    // Also keep a raw PianoRoll pointer for coordinate/data access.
    pianoRolls.push_back(ptr->getPianoRoll());

    if (createUndo && project->um && parentNode) {
        // Undo tracking uses the expanded window's logical ID.
        auto* action = new TogglePianoRollWindowAction(project,
            parentNode->nm->managerPath, static_cast<int>(parentNode->id),
            static_cast<int>(region->id), ew->id,
            0.f, 0.f, 800.f, 600.f, 0, true);
        project->um->newAction(action);
    }
}
