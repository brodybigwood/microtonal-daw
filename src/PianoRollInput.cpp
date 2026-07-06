#include "PianoRoll.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstdlib>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <string>
#include <SDL3/SDL.h>
#include "styles.h"
#include "GridView.h"
#include "Region.h"
#include "Note.h"
#include "Playhead.h"
#include "Transport.h"
#include "ContextMenu.h"
#include "Node.h"
#include "NodeManager.h"
#include "nodes/nodetypes.h"
#include "UndoManager.h"
#include "PianoRollInternal.h"

// Mouse/keyboard note editing: click handling, custom input, note creation,
// deletion, dragging and stretching (split from PianoRoll.cpp).

void PianoRoll::clickMouse(SDL_Event& e) {
    switch (e.type) {

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if(mouseY < topMargin) {
                    return;
                }
                if(mouseY > height - bottomMargin) {
                    if (mouseX + dstRect->x >= modeButtonRect.x &&
                        mouseX + dstRect->x <= modeButtonRect.x + modeButtonRect.w &&
                        mouseY + dstRect->y >= modeButtonRect.y &&
                        mouseY + dstRect->y <= modeButtonRect.y + modeButtonRect.h) {
                        const bool canSwitchToEdo =
                            tuningMode != TuningMode::Harmonic || regionHasDragDefinedEdoLattice(region);
                        if (canSwitchToEdo)
                            newTuning();
                    }
                    // Channel picker left arrow
                    if (mouseX + dstRect->x >= channelLeftRect.x &&
                        mouseX + dstRect->x <= channelLeftRect.x + channelLeftRect.w &&
                        mouseY + dstRect->y >= channelLeftRect.y &&
                        mouseY + dstRect->y <= channelLeftRect.y + channelLeftRect.h) {
                        if (currentChannel > 0) currentChannel--;
                        refreshGrid = true;
                    }
                    // Channel picker right arrow
                    if (mouseX + dstRect->x >= channelRightRect.x &&
                        mouseX + dstRect->x <= channelRightRect.x + channelRightRect.w &&
                        mouseY + dstRect->y >= channelRightRect.y &&
                        mouseY + dstRect->y <= channelRightRect.y + channelRightRect.h) {
                        currentChannel++;
                        refreshGrid = true;
                    }
                    return;
                }
                if (isCtrlPressed && isShiftPressed && mouseX > leftMargin) {
                    rhythmEdoDefineDialogOpen = false;
                    selectingRhythmInterval = true;
                    rhythmIntervalStartNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    refreshHoveredRhythmLineIndex();
                    if (rhythmIntervalStartNote)
                        rhythmIntervalStartSec = Note::secondsFromVector(rhythmIntervalStartNote->rhythmVector);
                    else if (hoveredRhythmLineIndex != SIZE_MAX && hoveredRhythmLineIndex < rhythmLines.size())
                        rhythmIntervalStartSec = rhythmLines[hoveredRhythmLineIndex].seconds;
                    else
                        rhythmIntervalStartSec = 0.0f;
                    rhythmIntervalEndSec = rhythmIntervalStartSec;
                    rhythmDragEndVertexPairs.clear();
                    if (rhythmIntervalStartNote)
                        rhythmDragStartVertexPairs = rhythmIntervalStartNote->rhythmVector;
                    else if (hoveredRhythmLineIndex != SIZE_MAX && hoveredRhythmLineIndex < rhythmLines.size())
                        rhythmDragStartVertexPairs = rhythmLines[hoveredRhythmLineIndex].integerPairs;
                    else
                        rhythmDragStartVertexPairs.clear();
                    rhythmIntervalDragMoved = false;
                    return;
                }
                if (isShiftPressed && mouseX > leftMargin) {
                    intervalEdoDefineDialogOpen = false;
                    selectingInterval = true;
                    intervalSelectStartedHarmonic = (tuningMode == TuningMode::Harmonic);
                    intervalStartNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    intervalStartLine = intervalStartNote ? intervalStartNote->num : getHoveredLine();
                    intervalEndLine = intervalStartLine;
                    intervalDragEndVertexPairs.clear();
                    if (intervalStartNote)
                        intervalDragStartVertexPairs = intervalStartNote->pitchVector;
                    else
                        intervalDragStartVertexPairs.clear();
                    intervalDragMoved = false;
                    if (tuningMode == TuningMode::Harmonic) {
                        intervalDragHarmA =
                            intervalStartNote ? structuralHarmonicNearNote(intervalStartNote) : hoveredHarmonicFromGrid();
                        intervalDragHarmB = intervalDragHarmA;
                        intervalDragEdoKA = INT_MAX;
                        intervalDragEdoKB = INT_MAX;
                    } else {
                        intervalDragEdoKA =
                            intervalStartNote ? structuralEdoKNearNote(intervalStartNote) : hoveredEdoKFromGrid();
                        intervalDragEdoKB = intervalDragEdoKA;
                        intervalDragHarmA = 0;
                        intervalDragHarmB = 0;
                    }
                    return;
                }
                if (isCtrlPressed && mouseX > leftMargin) {
                    // Check for background-channel note first: ctrl+click switches channel
                    if (!isShiftPressed) {
                        for (auto& note : region->notes) {
                            if (note->channel == currentChannel) continue;
                            const int notePosX = getNotePosX(note);
                            const int noteEnd = getNoteEnd(note);
                            const int noteY = getY(noteMidiForRender(note));
                            if (mouseX >= notePosX && mouseX <= noteEnd &&
                                mouseY <= noteY + noteRadius && mouseY >= (noteY - noteRadius)) {
                                currentChannel = note->channel;
                                handleMouse();
                                refreshGrid = true;
                                return;
                            }
                        }
                    }
                    getExistingNote();
                    if (hoveredElement != nullptr) {
                        auto n = std::dynamic_pointer_cast<Note>(hoveredElement);
                        if (n) {
                            if (selectedNoteIds.count(n->id))
                                selectedNoteIds.erase(n->id);
                            else
                                selectedNoteIds.insert(n->id);
                        }
                    } else {
                        selectingRubberBand = true;
                        rubberBandStartX = mouseX;
                        rubberBandStartY = mouseY;
                        rubberBandEndX = mouseX;
                        rubberBandEndY = mouseY;
                    }
                    handleMouse();
                    refreshGrid = true;
                    return;
                }
                if (mouseX > leftMargin) {
                    getExistingNote();
                    getStretchingNote();
                    if (stretchingNote != nullptr) {
                        stretchingNoteUndoBefore = stretchingNote->toJSON();
                        stretchingNoteHasUndoSnapshot = true;
                        stretchingNoteDragDirty = false;
                        // Multi-note stretch: if stretched note is selected, snapshot all selected
                        if (selectedNoteIds.count(stretchingNote->id) && selectedNoteIds.size() > 1) {
                            stretchingMultipleNotes = true;
                            multiStretchBefores.clear();
                            for (auto& sn : region->notes) {
                                if (selectedNoteIds.count(sn->id))
                                    multiStretchBefores[sn->id] = sn->toJSON();
                            }
                        } else {
                            stretchingMultipleNotes = false;
                            multiStretchBefores.clear();
                        }
                        last_lmb_x = mouseX;
                        handleMouse();
                        refreshGrid = true;
                        return;
                    }
                    if (hoveredElement == nullptr) {
                        selectedNoteIds.clear();
                        createElement();
                    } else {
                        auto n = std::dynamic_pointer_cast<Note>(hoveredElement);
                        if (isShiftPressed) {
                            if (!n) return;
                            auto before = captureRegionTuning(region);
                            auto after = before;
                            after.mode = n->tuningMode;
                            after.harmonicAnchorNumber = n->tuningAnchorHarmonic;
                            if (after.mode == 0) {
                                after.harmonicAnchorVector = n->pitchVector;
                            } else {
                                after.harmonicAnchorVector.clear();
                                after.edoSubdivisionSteps = n->tuningEdoSubdivisionSteps;
                                after.edoLowerVector = n->tuningEdoLowerVector;
                                after.edoUpperVector = n->tuningEdoUpperVector;
                            }
                            project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
                                region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
                                regionTuningSnapshotToUndoJson(after), "Recall Note Tuning View"));
                            return;
                        }
                        // Multi-note move: if clicked note is in selection, move all selected
                        if (selectedNoteIds.count(n->id) && selectedNoteIds.size() > 1) {
                            movingMultipleNotes = true;
                            multiMoveBefores.clear();
                            multiPitchPreviews.clear();
                            movingNote = n;
                            movingNoteUndoBefore = n->toJSON();
                            movingNoteHasUndoSnapshot = true;
                            movingNoteDragDirty = false;
                            movingNotePitchPreviewLineMidi.reset();
                            movingNoteRhythmPreviewLineIdx.reset();
                            // Store start pairs: prefer hovered note's pairs, else grid line
                            dragStartPairs = n->pitchVector;
                            rhythmDragStartPairs = n->rhythmVector;
                            rhythmDragEndPairs = n->rhythmEndVector;
                            rhythmDragGrabOffsetPx = mouseX - getNotePosX(n);
                            for (auto& sn : region->notes) {
                                if (selectedNoteIds.count(sn->id))
                                    multiMoveBefores[sn->id] = sn->toJSON();
                            }
                        } else {
                            // Single note move (clear selection)
                            selectedNoteIds.clear();
                            movingMultipleNotes = false;
                            multiMoveBefores.clear();
                            multiPitchPreviews.clear();
                            movingNote = n;
                            movingNoteUndoBefore = n->toJSON();
                            movingNoteHasUndoSnapshot = true;
                            movingNoteDragDirty = false;
                            movingNotePitchPreviewLineMidi.reset();
                            movingNoteRhythmPreviewLineIdx.reset();
                            dragStartPairs = n->pitchVector;
                            rhythmDragStartPairs = n->rhythmVector;
                            rhythmDragEndPairs = n->rhythmEndVector;
                            rhythmDragGrabOffsetPx = mouseX - getNotePosX(n);
                        }
                        last_lmb_x = mouseX;
                    }
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                if(mouseX > leftMargin && stretchingNote == nullptr) {
                    if(isCtrlPressed && isShiftPressed && hoveredElement != nullptr) {
                        auto note = std::dynamic_pointer_cast<Note>(hoveredElement);
                        if (note && region) {
                            auto before = captureRegionTuning(region);
                            auto after = before;
                            after.rhythmEdoSubdivisionSteps = note->rhythmEdoSubdivisionSteps;
                            after.rhythmEdoLowerVector = note->rhythmEdoLowerVector;
                            after.rhythmEdoUpperVector = note->rhythmEdoUpperVector;
                            project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
                                region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
                                regionTuningSnapshotToUndoJson(after), "Recall Note Rhythm View"));
                        }
                        rmb = false;
                    }
                    else if(isShiftPressed && hoveredElement != nullptr) {
                        auto ctxMenu = ContextMenu::get();
                        ctxMenu->skipNextEvent = true;
                        if (project && project->window)
                            SDL_StartTextInput(project->window);
                        ctxMenu->activate();

                        ctxMenu->dynamicTick = getTextInputTicker([this](std::string text) {
                            try {
                                const int h = std::max(1, std::stoi(text));
                                if (!hoveredElement) return;
                                auto note = std::dynamic_pointer_cast<Note>(hoveredElement);
                                if (!note) return;
                                auto beforeRegion = captureRegionTuning(region);
                                auto beforeNote = captureNoteTuning(note);
                                auto afterRegion = beforeRegion;
                                afterRegion.mode = 0;
                                afterRegion.harmonicAnchorNumber = h;
                                afterRegion.harmonicAnchorVector = note->pitchVector;
                                auto afterNote = beforeNote;
                                afterNote.tuningMode = 0;
                                afterNote.tuningAnchorHarmonic = h;
                                project->um->newAction(new AssignNoteHarmonicUndoAction(project, region->parentNode->nm->managerPath,
                                    region->parentNode->id, region->id, note->id, regionTuningSnapshotToUndoJson(beforeRegion),
                                    regionTuningSnapshotToUndoJson(afterRegion), noteTuningSnapshotToUndoJson(beforeNote),
                                    noteTuningSnapshotToUndoJson(afterNote)));
                                isShiftPressed = false;
                                rmb = false;
                            } catch (...) {
                            }
                        },
                            [this]() { rmb = false; }
                        );
                    } else {
                        deleteElement();
                    }
                }

            }
            handleMouse();
            refreshGrid = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = false;
                // Rubber band selection finalize
                if (selectingRubberBand) {
                    selectingRubberBand = false;
                    float bx1 = std::min(rubberBandStartX, rubberBandEndX);
                    float by1 = std::min(rubberBandStartY, rubberBandEndY);
                    float bx2 = std::max(rubberBandStartX, rubberBandEndX);
                    float by2 = std::max(rubberBandStartY, rubberBandEndY);
                    for (auto& note : region->notes) {
                        if (note->channel != currentChannel) continue;
                        float nx = getNotePosX(note);
                        float ne = getNoteEnd(note);
                        float ny = getY(noteMidiForRender(note));
                        if (ne >= bx1 && nx <= bx2 && ny + noteRadius >= by1 && ny - noteRadius <= by2)
                            selectedNoteIds.insert(note->id);
                    }
                    refreshGrid = true;
                    break;
                }
                // Multi-note stretch commit
                if (stretchingMultipleNotes && stretchingNoteHasUndoSnapshot) {
                    std::vector<int> ids;
                    std::vector<json> befores, afters;
                    bool anyDirty = stretchingNoteDragDirty;
                    for (auto& [nid, before] : multiStretchBefores) {
                        auto it = region->id_to_index.find(nid);
                        if (it == region->id_to_index.end()) continue;
                        const size_t idx = static_cast<size_t>(it->second);
                        if (idx >= region->notes.size() || !region->notes[idx]) continue;
                        json after = region->notes[idx]->toJSON();
                        if (after != before) anyDirty = true;
                        ids.push_back(nid);
                        befores.push_back(std::move(before));
                        afters.push_back(std::move(after));
                    }
                    if (anyDirty && !ids.empty()) {
                        project->um->newAction(new MoveMultipleNotesAction(project, region->parentNode->nm->managerPath,
                            region->parentNode->id, region->id, std::move(ids), std::move(befores), std::move(afters), "Resize Notes"));
                    }
                    stretchingNoteHasUndoSnapshot = false;
                    stretchingNoteDragDirty = false;
                    stretchingMultipleNotes = false;
                    multiStretchBefores.clear();
                } else if (stretchingNoteHasUndoSnapshot && stretchingNote) {
                    json afterStretch = stretchingNote->toJSON();
                    if (stretchingNoteDragDirty || afterStretch != stretchingNoteUndoBefore) {
                        project->um->newAction(new MoveNoteAction(project, region->parentNode->nm->managerPath,
                            region->parentNode->id, region->id, stretchingNote->id, std::move(stretchingNoteUndoBefore),
                            std::move(afterStretch), "Resize Note"));
                    }
                    stretchingNoteHasUndoSnapshot = false;
                    stretchingNoteDragDirty = false;
                }
                // Multi-note move commit
                if (movingMultipleNotes && movingNoteHasUndoSnapshot && movingNote) {
                    // Apply pitch previews from integer pairs
                    for (auto& [nid, previewPairs] : multiPitchPreviews) {
                        auto it = region->id_to_index.find(nid);
                        if (it == region->id_to_index.end()) continue;
                        const size_t idx = static_cast<size_t>(it->second);
                        if (idx >= region->notes.size() || !region->notes[idx]) continue;
                        auto& note = region->notes[idx];
                        note->pitchVector = previewPairs;
                        note->syncNumFromPitchVector();
                    }
                    // Also handle the primary note's pitch preview if it was single-tracked
                    if (movingNotePitchPreviewLineMidi) {
                        commitNotePitchSnap(movingNote, *movingNotePitchPreviewLineMidi);
                        movingNotePitchPreviewLineMidi.reset();
                    }
                    if (movingNoteRhythmPreviewLineIdx) {
                        const size_t rli = *movingNoteRhythmPreviewLineIdx;
                        if (rli < rhythmLines.size()) {
                            const auto& previewPairs = rhythmLines[rli].integerPairs;
                            const auto startDelta = subVec(previewPairs, rhythmDragStartPairs);
                            for (auto& [nid, before] : multiMoveBefores) {
                                auto it = region->id_to_index.find(nid);
                                if (it == region->id_to_index.end()) continue;
                                const size_t idx = static_cast<size_t>(it->second);
                                if (idx >= region->notes.size() || !region->notes[idx]) continue;
                                auto& sn = region->notes[idx];
                                sn->rhythmVector = addVec(sn->rhythmVector, startDelta);
                                sn->rhythmEndVector = addVec(sn->rhythmEndVector, startDelta);
                                if (region) {
                                    sn->rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
                                    sn->rhythmEdoLowerVector = region->rhythmEdoLowerVector;
                                    sn->rhythmEdoUpperVector = region->rhythmEdoUpperVector;
                                }
                            }
                        }
                        movingNoteRhythmPreviewLineIdx.reset();
                    }
                    // Build multi-note undo action
                    std::vector<int> ids;
                    std::vector<json> befores, afters;
                    bool anyDirty = movingNoteDragDirty;
                    for (auto& [nid, before] : multiMoveBefores) {
                        auto it = region->id_to_index.find(nid);
                        if (it == region->id_to_index.end()) continue;
                        const size_t idx = static_cast<size_t>(it->second);
                        if (idx >= region->notes.size() || !region->notes[idx]) continue;
                        json after = region->notes[idx]->toJSON();
                        if (after != before) anyDirty = true;
                        ids.push_back(nid);
                        befores.push_back(std::move(before));
                        afters.push_back(std::move(after));
                    }
                    if (anyDirty && !ids.empty()) {
                        project->um->newAction(new MoveMultipleNotesAction(project, region->parentNode->nm->managerPath,
                            region->parentNode->id, region->id, std::move(ids), std::move(befores), std::move(afters)));
                    }
                    movingNoteHasUndoSnapshot = false;
                    movingNoteDragDirty = false;
                    movingMultipleNotes = false;
                    multiMoveBefores.clear();
                    multiPitchPreviews.clear();
                } else if (movingNoteHasUndoSnapshot && movingNote) {
                    if (movingNotePitchPreviewLineMidi) {
                        commitNotePitchSnap(movingNote, *movingNotePitchPreviewLineMidi);
                        movingNotePitchPreviewLineMidi.reset();
                    }
                    if (movingNoteRhythmPreviewLineIdx) {
                        const size_t rli = *movingNoteRhythmPreviewLineIdx;
                        if (rli < rhythmLines.size()) {
                            movingNote->rhythmVector = rhythmLines[rli].integerPairs;
                            const auto delta = subVec(rhythmLines[rli].integerPairs, rhythmDragStartPairs);
                            auto newEndPairs = addVec(rhythmDragEndPairs, delta);
                            const size_t erli = closestRhythmLineIndexForSeconds(Note::secondsFromVector(newEndPairs));
                            if (erli != SIZE_MAX && erli < rhythmLines.size())
                                movingNote->rhythmEndVector = rhythmLines[erli].integerPairs;
                            else
                                movingNote->rhythmEndVector = std::move(newEndPairs);
                            if (region) {
                                movingNote->rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
                                movingNote->rhythmEdoLowerVector = region->rhythmEdoLowerVector;
                                movingNote->rhythmEdoUpperVector = region->rhythmEdoUpperVector;
                            }
                        }
                        movingNoteRhythmPreviewLineIdx.reset();
                    }
                    json after = movingNote->toJSON();
                    if (movingNoteDragDirty || after != movingNoteUndoBefore) {
                        project->um->newAction(new MoveNoteAction(project, region->parentNode->nm->managerPath,
                            region->parentNode->id, region->id, movingNote->id, std::move(movingNoteUndoBefore),
                            std::move(after)));
                    }
                    movingNoteHasUndoSnapshot = false;
                    movingNoteDragDirty = false;
                }
                movingNote = nullptr;
                if (selectingRhythmInterval) {
                    auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    const bool sameNoteClick = !rhythmIntervalDragMoved &&
                        rhythmIntervalStartNote && endNote && (rhythmIntervalStartNote->id == endNote->id);
                    selectingRhythmInterval = false;
                    rhythmEdoDefineDialogOpen = false;
                    if (sameNoteClick) {
                        auto before = captureRegionTuning(region);
                        auto after = before;
                        after.rhythmEdoSubdivisionSteps = rhythmIntervalStartNote->rhythmEdoSubdivisionSteps;
                        after.rhythmEdoLowerVector = rhythmIntervalStartNote->rhythmEdoLowerVector;
                        after.rhythmEdoUpperVector = rhythmIntervalStartNote->rhythmEdoUpperVector;
                        project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
                            region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
                            regionTuningSnapshotToUndoJson(after), "Apply Note Rhythm View"));
                        rhythmIntervalStartNote = nullptr;
                        refreshGrid = true;
                        return;
                    }
                    if (std::fabs(rhythmIntervalEndSec - rhythmIntervalStartSec) < 0.001f) {
                        rhythmIntervalStartNote = nullptr;
                        refreshGrid = true;
                        return;
                    }
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
                            auto before = captureRegionTuning(region);
                            auto after = before;
                            after.rhythmEdoSubdivisionSteps = steps;
                            after.rhythmEdoLowerVector = lowerPairs;
                            after.rhythmEdoUpperVector = upperPairs;
                            project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
                                region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
                                regionTuningSnapshotToUndoJson(after), "Define Rhythm EDO"));
                        } catch (...) {}
                    },
                        [this]() { rhythmEdoDefineDialogOpen = false; });
                    rhythmIntervalStartNote = nullptr;
                }
                if (selectingInterval) {
                    auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    const bool sameNoteClick = !intervalDragMoved &&
                        intervalStartNote && endNote && (intervalStartNote->id == endNote->id);
                    if (sameNoteClick) {
                        selectingInterval = false;
                        intervalEdoDefineDialogOpen = false;
                        auto before = captureRegionTuning(region);
                        applyNoteTuning(intervalStartNote);
                        auto after = captureRegionTuning(region);
                        if (regionTuningSnapshotToUndoJson(before) != regionTuningSnapshotToUndoJson(after)) {
                            project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
                                region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
                                regionTuningSnapshotToUndoJson(after), "Apply Note Tuning (Interval)"));
                        }
                        intervalStartNote = nullptr;
                        refreshGrid = true;
                        return;
                    }
                    if (std::fabs(intervalEndLine - intervalStartLine) < 0.01f) {
                        selectingInterval = false;
                        intervalEdoDefineDialogOpen = false;
                        intervalStartNote = nullptr;
                        refreshGrid = true;
                        return;
                    }
                    intervalDialogFrozenStartLine = intervalStartLine;
                    intervalDialogFrozenEndLine = intervalEndLine;
                    intervalDialogFrozenStartVertexPairs = intervalDragStartVertexPairs;
                    intervalDialogFrozenEndVertexPairs = intervalDragEndVertexPairs;
                    intervalEdoDefineDialogOpen = true;
                    selectingInterval = false;
                    bool intervalFromTwoNotes = false;
                    std::vector<std::pair<int, int>> intervalLoNotePairs;
                    std::vector<std::pair<int, int>> intervalHiNotePairs;
                    if (intervalStartNote && endNote && intervalDragMoved &&
                        intervalStartNote->id != endNote->id) {
                        intervalFromTwoNotes = true;
                        if (intervalStartNote->num <= endNote->num) {
                            intervalLoNotePairs = intervalStartNote->pitchVector;
                            intervalHiNotePairs = endNote->pitchVector;
                        } else {
                            intervalLoNotePairs = endNote->pitchVector;
                            intervalHiNotePairs = intervalStartNote->pitchVector;
                        }
                    }
                    auto ctxMenu = ContextMenu::get();
                    ctxMenu->skipNextEvent = true;
                    if (project && project->window)
                        SDL_StartTextInput(project->window);
                    ctxMenu->activate();
                    const float a = intervalDialogFrozenStartLine;
                    const float b = intervalDialogFrozenEndLine;
                    const std::shared_ptr<Note> capIntervalStartNote = intervalStartNote;
                    const std::shared_ptr<Note> capIntervalEndNote = endNote;
                    const std::vector<std::pair<int, int>> capDragStartVertexPairs = intervalDialogFrozenStartVertexPairs;
                    const std::vector<std::pair<int, int>> capDragEndVertexPairs = intervalDialogFrozenEndVertexPairs;
                    ctxMenu->dynamicTick = getTextInputTicker(
                        [this, a, b, intervalFromTwoNotes,
                            intervalLoNotePairs, intervalHiNotePairs, capIntervalStartNote, capIntervalEndNote,
                            capDragStartVertexPairs, capDragEndVertexPairs](std::string text) {
                        try {
                            const int steps = std::max(1, std::stoi(text));
                            // Snapshots track rational endpoints while dragging; b can be off-grid note MIDI with
                            // hoveredElement on the staff (endNote null) — never pitchVectorAtGridMidi(note->num).
                            const std::vector<std::pair<int, int>> vecAtA =
                                capIntervalStartNote ? capDragStartVertexPairs : pitchVectorAtGridMidi(a);
                            const std::vector<std::pair<int, int>> vecAtB =
                                !capDragEndVertexPairs.empty()
                                    ? capDragEndVertexPairs
                                    : (capIntervalEndNote ? capIntervalEndNote->pitchVector
                                                          : pitchVectorAtGridMidi(b));
                            const std::vector<std::pair<int, int>> lowerLineVec =
                                intervalFromTwoNotes ? intervalLoNotePairs : (a <= b ? vecAtA : vecAtB);
                            const std::vector<std::pair<int, int>> upperLineVec =
                                intervalFromTwoNotes ? intervalHiNotePairs : (a <= b ? vecAtB : vecAtA);

                            auto before = captureRegionTuning(region);
                            auto after = before;

                            after.mode = 1;
                            after.edoLowerVector = lowerLineVec;
                            after.edoUpperVector = upperLineVec;
                            after.edoSubdivisionSteps = steps;
                            after.harmonicAnchorVector.clear();
                            project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
                                region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
                                regionTuningSnapshotToUndoJson(after), "Define EDO Interval"));
                        } catch (...) {
                        }
                    },
                        [this]() {
                            intervalEdoDefineDialogOpen = false;
                        });
                    intervalStartNote = nullptr;
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = false;
            }
            handleMouse();
            break;
    }
}

void PianoRoll::handleCustomInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            Scroll();
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            width = e.window.data1;  // New width
            height = e.window.data2; // New height
            needsInit_ = true;
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            width = e.window.data1;  // New width
            height = e.window.data2; // New height
            needsInit_ = true;
            break;

        case SDL_EVENT_KEY_DOWN:

            switch (e.key.scancode) {
                case SDL_SCANCODE_DELETE:
                case SDL_SCANCODE_BACKSPACE:
                    deleteSelection();
                    break;
                case SDL_SCANCODE_MINUS:
                    notesPerOctave -= 1;
                    UpdateGrid();
                    break;
                case SDL_SCANCODE_EQUALS:
                    notesPerOctave += 1;
                    UpdateGrid();
                    break;
                default:
                    break;
            }
            break;

        

        case SDL_EVENT_MOUSE_MOTION:

            handleMouse();

            if (selectingRhythmInterval) {
                auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                refreshHoveredRhythmLineIndex();
                if (endNote) {
                    rhythmIntervalEndSec = Note::secondsFromVector(endNote->rhythmVector);
                    rhythmDragEndVertexPairs = endNote->rhythmVector;
                    if (!intervalStartNote || endNote->id != intervalStartNote->id)
                        rhythmIntervalDragMoved = true;
                } else if (hoveredRhythmLineIndex != SIZE_MAX && hoveredRhythmLineIndex < rhythmLines.size()) {
                    rhythmIntervalEndSec = rhythmLines[hoveredRhythmLineIndex].seconds;
                    rhythmDragEndVertexPairs = rhythmLines[hoveredRhythmLineIndex].integerPairs;
                    if (std::fabs(rhythmIntervalEndSec - rhythmIntervalStartSec) > 0.001f)
                        rhythmIntervalDragMoved = true;
                }
                refreshGrid = true;
                break;
            }

            if (selectingInterval) {
                auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                if (endNote) {
                    intervalEndLine = endNote->num;
                    intervalDragEndVertexPairs = endNote->pitchVector;
                    if (!intervalStartNote || endNote->id != intervalStartNote->id) {
                        intervalDragMoved = true;
                    }
                } else {
                    // Always follow the hovered lattice line. The old dNearest<dStart rule kept intervalEndLine
                    // pinned to intervalStartLine while the cursor was still nearer in Y to the start note, so loF
                    // stayed at the note MIDI and pitchVectorAtGridMidi(loF) used the wrong lattice row.
                    const float nearest = getHoveredLine();
                    intervalEndLine = nearest;
                    intervalDragEndVertexPairs.clear();
                    if (std::fabs(intervalEndLine - intervalStartLine) > 0.01f)
                        intervalDragMoved = true;
                }
                if (intervalSelectStartedHarmonic) {
                    if (endNote)
                        intervalDragHarmB = structuralHarmonicNearNote(endNote);
                    else if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lineStructural.size())
                        intervalDragHarmB = std::max(1, lineStructural[hoveredPitchLineIndex]);
                    else
                        intervalDragHarmB = hoveredHarmonicFromGrid();
                } else {
                    if (endNote)
                        intervalDragEdoKB = structuralEdoKNearNote(endNote);
                    else if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lineStructural.size())
                        intervalDragEdoKB = lineStructural[hoveredPitchLineIndex];
                    else
                        intervalDragEdoKB = hoveredEdoKFromGrid();
                }
                refreshGrid = true;
                break;
            }

            // Rubber band drag
            if (selectingRubberBand && lmb) {
                rubberBandEndX = mouseX;
                rubberBandEndY = mouseY;
                refreshGrid = true;
                break;
            }

            
            if(stretchingNote != nullptr) {
                if(!lmb) {
                    isStretchingNote = false;
                } else {
                    refreshGrid = true;
                    // Snap the dragged edge to the nearest grid line on every
                    // motion event (no pixel-step gating).
                    const auto beforeStart = stretchingNote->rhythmVector;
                    const auto beforeEnd = stretchingNote->rhythmEndVector;
                    stretchElement(0);
                    const bool moved = stretchingNote->rhythmVector != beforeStart ||
                                       stretchingNote->rhythmEndVector != beforeEnd;
                    if (moved) {
                        stretchingNoteDragDirty = true;
                        if (stretchingMultipleNotes) {
                            const auto startDelta = subVec(stretchingNote->rhythmVector, beforeStart);
                            const auto endDelta = subVec(stretchingNote->rhythmEndVector, beforeEnd);
                            for (auto& sn : region->notes) {
                                if (sn->id == stretchingNote->id) continue;
                                if (!selectedNoteIds.count(sn->id)) continue;
                                sn->rhythmVector = addVec(sn->rhythmVector, startDelta);
                                sn->rhythmEndVector = addVec(sn->rhythmEndVector, endDelta);
                                sn->rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
                                sn->rhythmEdoLowerVector = region->rhythmEdoLowerVector;
                                sn->rhythmEdoUpperVector = region->rhythmEdoUpperVector;
                            }
                        }
                    }
                }

            } else if(lmb && movingNote != nullptr) {
                refreshGrid = true;

                float dX = mouseX - last_lmb_x;
                float step = dW / notesPerBar;
                if(dX != 0.0f) {
                    if (movingMultipleNotes) {
                        for (auto& sn : region->notes) {
                            if (selectedNoteIds.count(sn->id))
                                moveNoteTime(sn);
                        }
                    } else {
                        moveNoteTime(movingNote);
                    }
                    last_lmb_x += dX;
                }

                // Pitch preview: compute delta in integer pairs, apply same delta to all selected notes
                if (movingMultipleNotes &&
                    hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < pitchLines.size()) {
                    auto& currentPairs = pitchLines[hoveredPitchLineIndex].integerPairs;
                    size_t maxLen = std::max(dragStartPairs.size(), currentPairs.size());
                    std::vector<std::pair<int,int>> deltaPairs(maxLen, {0, 1});
                    for (size_t i = 0; i < maxLen; ++i) {
                        auto a = i < dragStartPairs.size() ? dragStartPairs[i] : std::pair<int,int>{0, 1};
                        auto b = i < currentPairs.size() ? currentPairs[i] : std::pair<int,int>{0, 1};
                        deltaPairs[i] = ratSub(b, a);
                    }
                    bool allZero = true;
                    for (auto& d : deltaPairs)
                        if (d.first != 0) { allZero = false; break; }
                    if (!allZero) {
                        for (auto& sn : region->notes) {
                            if (!selectedNoteIds.count(sn->id)) continue;
                            size_t noteMax = std::max(sn->pitchVector.size(), deltaPairs.size());
                            std::vector<std::pair<int,int>> preview(noteMax, {0, 1});
                            for (size_t i = 0; i < noteMax; ++i) {
                                auto a = i < sn->pitchVector.size() ? sn->pitchVector[i] : std::pair<int,int>{0, 1};
                                auto b = i < deltaPairs.size() ? deltaPairs[i] : std::pair<int,int>{0, 1};
                                preview[i] = ratAdd(a, b);
                            }
                            multiPitchPreviews[sn->id] = std::move(preview);
                        }
                    } else {
                        multiPitchPreviews.clear();
                    }
                } else if (!movingMultipleNotes) {
                    constexpr float pitchDragEpsilon = 1e-3f;
                    const float snappedLine = getHoveredLine();
                    if (std::fabs(snappedLine - movingNote->num) > pitchDragEpsilon)
                        movingNotePitchPreviewLineMidi = snappedLine;
                    else
                        movingNotePitchPreviewLineMidi.reset();
                }
                
            } else {
                last_lmb_x = mouseX;
                last_lmb_y = mouseY;
            }

            break;


        // Optionally handle other events you might need:
        default:
            refreshGrid = false;
            break;
    }
}

void PianoRoll::createElement() {
    refreshHoveredPitchLineIndex();
    refreshHoveredRhythmLineIndex();
    const size_t li = hoveredPitchLineIndex;
    const size_t rli = hoveredRhythmLineIndex;
    std::vector<std::pair<int, int>> pitchPairs;
    std::vector<std::pair<int, int>> rhythmPairs;
    if (li != SIZE_MAX && li < pitchLines.size())
        pitchPairs = pitchLines[li].integerPairs;
    if (rli != SIZE_MAX && rli < rhythmLines.size())
        rhythmPairs = rhythmLines[rli].integerPairs;
    const int anchorHarmonic = (tuningMode == TuningMode::Harmonic && li != SIZE_MAX && li < lineStructural.size())
        ? std::max(1, lineStructural[li]) : 1;
    std::vector<std::pair<int,int>> endPairs;
    if (!lastRhythmDurationPairs.empty())
        endPairs = addVec(rhythmPairs, lastRhythmDurationPairs);
    else if (rli != SIZE_MAX && rli + 1 < rhythmLines.size())
        endPairs = rhythmLines[rli + 1].integerPairs;
    else
        endPairs = rhythmPairs;
    project->createNote(
        region->parentNode->id,
        rhythmPairs,
        endPairs,
        region->id,
        region->parentNode->nm->managerPath,
        std::move(pitchPairs)
    );
    if (!region->notes.empty()) {
        auto note = region->notes.back();
        note->channel = currentChannel;
        note->tuningAnchorHarmonic = anchorHarmonic;
        if (tuningMode == TuningMode::EDO) {
            note->tuningEdoSubdivisionSteps = region->tuningEdoSubdivisionSteps;
            note->tuningEdoLowerVector = region->tuningEdoLowerVector;
            note->tuningEdoUpperVector = region->tuningEdoUpperVector;
        }
        refreshHoveredRhythmLineIndex();
        const size_t rli = hoveredRhythmLineIndex;
        if (rli != SIZE_MAX && rli < rhythmLines.size())
            note->rhythmVector = rhythmLines[rli].integerPairs;
        note->rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
        note->rhythmEdoLowerVector = region->rhythmEdoLowerVector;
        note->rhythmEdoUpperVector = region->rhythmEdoUpperVector;
        lastRhythmDurationPairs = subVec(note->rhythmEndVector, note->rhythmVector);
        stampNoteTuning(note);
        // CreateNoteAction::doAction does not rerun stampNoteTuning on redo; persist post-stamp state on the action.
        if (project && project->um && project->um->current && project->um->current->type == CreateNote)
            static_cast<CreateNoteAction*>(project->um->current)->noteStampedSnapshot = note->toJSON();
    }
    refreshGrid = true;
}

void PianoRoll::deleteElement() {
    if (hoveredElement == nullptr)
        return;
    auto note = std::dynamic_pointer_cast<Note>(hoveredElement);
    if (!note)
        return;
    selectedNoteIds.erase(note->id);
    const int nid = note->id;
    hoveredElement = nullptr;
    project->deleteNote(region->parentNode->id, region->id, nid, region->parentNode->nm->managerPath);
    Scroll();
}

void PianoRoll::deleteSelection() {
    if (selectedNoteIds.empty())
        return;
    std::vector<int> toDelete(selectedNoteIds.begin(), selectedNoteIds.end());
    selectedNoteIds.clear();
    hoveredElement = nullptr;
    for (int nid : toDelete)
        project->deleteNote(region->parentNode->id, region->id, nid, region->parentNode->nm->managerPath);
    Scroll();
}


void PianoRoll::handleMouse() {
    getStretchingNote();
    getExistingNote();

    if(rmb) {
        SDL_SetCursor(cursors.pencil);
        if(hoveredElement != nullptr && !ContextMenu::get()->active) {
            deleteElement();
        }
    } else {
        if (stretchingNote != nullptr) {
            SDL_SetCursor(cursors.resize);
        } else if(hoveredElement != nullptr) {
            SDL_SetCursor(cursors.mover);
        } else {
            SDL_SetCursor(cursors.selector);
        }
    }

    refreshHoveredPitchLineIndex();
    refreshPitchFactorsHoverTiming();
}

bool PianoRoll::getStretchingNote() {
    if(movingNote != nullptr) {
        return false;
    }
    if(isStretchingNote) {
        return true;
    }
    for (std::shared_ptr<Note> note : region->notes) {
        if (note->channel != currentChannel) continue;

        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(noteMidiForRender(note));

        if ((mouseY >= noteY - noteRadius && mouseY <= (noteY + noteRadius))) {
            const float edgeZone = std::min(noteRadius, (noteEnd - notePosX) * 0.3f);
            if(mouseX >= notePosX - edgeZone && mouseX <= notePosX + edgeZone) {
                stretchingNote = note;
                resizeDir = -1;
                isStretchingNote = true;
                return true;
            } else if (mouseX >= noteEnd - edgeZone && mouseX <= noteEnd + edgeZone) {
                stretchingNote = note;
                resizeDir = 1;
                isStretchingNote = true;
                return true;
            }
        }
    }
    stretchingNote = nullptr;
    isStretchingNote = false;
    return false;
}

void PianoRoll::stretchElement(int amount) {
    if(stretchingNote == nullptr) {
        return;
    }
    if (amount != 0)
        stretchingNoteDragDirty = true;
    refreshHoveredRhythmLineIndex();
    if (hoveredRhythmLineIndex != SIZE_MAX && hoveredRhythmLineIndex < rhythmLines.size()) {
        const float lineSec = rhythmLines[hoveredRhythmLineIndex].seconds;
        if (resizeDir == -1 && lineSec < stretchingNote->endSeconds() - 0.001f) {
            stretchingNote->rhythmVector = rhythmLines[hoveredRhythmLineIndex].integerPairs;
            // Re-anchor the note onto this line: adopt the grid's rhythm temperament.
            if (region) {
                stretchingNote->rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
                stretchingNote->rhythmEdoLowerVector = region->rhythmEdoLowerVector;
                stretchingNote->rhythmEdoUpperVector = region->rhythmEdoUpperVector;
            }
        } else if (resizeDir == 1 && lineSec > stretchingNote->startSeconds() + 0.001f) {
            stretchingNote->rhythmEndVector = rhythmLines[hoveredRhythmLineIndex].integerPairs;
            if (region) {
                stretchingNote->rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
                stretchingNote->rhythmEdoLowerVector = region->rhythmEdoLowerVector;
                stretchingNote->rhythmEdoUpperVector = region->rhythmEdoUpperVector;
            }
        }
    }
    lastRhythmDurationPairs = subVec(stretchingNote->rhythmEndVector, stretchingNote->rhythmVector);
    Scroll();
}


float PianoRoll::noteMidiForRender(const std::shared_ptr<Note>& note) const {
    // Multi-note pitch preview (integer pairs)
    auto pit = multiPitchPreviews.find(note->id);
    if (pit != multiPitchPreviews.end())
        return Note::midiFromPitchVector(pit->second);
    // Single-note pitch preview (legacy path)
    if (movingNote && movingNote.get() == note.get() && movingNotePitchPreviewLineMidi)
        return *movingNotePitchPreviewLineMidi;
    return note->num;
}

void PianoRoll::moveNoteTime(std::shared_ptr<Note> note) {
    movingNoteDragDirty = true;
    mouseX -= rhythmDragGrabOffsetPx;
    refreshHoveredRhythmLineIndex();
    mouseX += rhythmDragGrabOffsetPx;
    if (hoveredRhythmLineIndex != SIZE_MAX && hoveredRhythmLineIndex < rhythmLines.size())
        movingNoteRhythmPreviewLineIdx = hoveredRhythmLineIndex;
    Scroll();
}

void PianoRoll::commitNotePitchSnap(std::shared_ptr<Note> note, float targetLineMidi) {
    const size_t li = closestLineIndexForMidi(targetLineMidi);
    if (li != SIZE_MAX && li < pitchLines.size()) {
        note->pitchVector = pitchLines[li].integerPairs;
        if (tuningMode == TuningMode::Harmonic && li < lineStructural.size()) {
            note->tuningAnchorHarmonic = std::max(1, lineStructural[li]);
        } else {
            note->tuningAnchorHarmonic = 1;
        }
        if (tuningMode == TuningMode::EDO) {
            note->tuningEdoSubdivisionSteps = region->tuningEdoSubdivisionSteps;
            note->tuningEdoLowerVector = region->tuningEdoLowerVector;
            note->tuningEdoUpperVector = region->tuningEdoUpperVector;
        }
        movingNoteDragDirty = true;
    }
    stampNoteTuning(note);
    Scroll();
}

void PianoRoll::snapNoteRhythm(const std::shared_ptr<Note>& note) {
    if (!note || !region || rhythmLines.empty()) return;
    const size_t rli = closestRhythmLineIndexForSeconds(note->startSeconds());
    if (rli != SIZE_MAX && rli < rhythmLines.size()) {
        note->rhythmVector = rhythmLines[rli].integerPairs;
        note->rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
        note->rhythmEdoLowerVector = region->rhythmEdoLowerVector;
        note->rhythmEdoUpperVector = region->rhythmEdoUpperVector;
    }
}

