#include "PianoRoll.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <cstdio>
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

// True when the grid has a line exactly at the given start time (exact
// prime-exponent vector equality — no float comparison).
static bool gridHasLineAt(const std::vector<RhythmGridLine>& lines,
                          const std::vector<std::pair<int, int>>& startVec) {
    for (const auto& line : lines)
        if (line.integerPairs == startVec) return true;
    return false;
}

// Session-wide note clipboard (shared between piano rolls).
namespace {
struct NoteClipboard {
    bool valid = false;
    std::vector<json> notes;
    // Exact time vectors of the earliest start and latest end, so paste shifts are
    // pure vector arithmetic and never land beside a grid line.
    std::vector<std::pair<int, int>> firstStartVec;
    std::vector<std::pair<int, int>> lastEndVec;
};
NoteClipboard g_noteClip;
float g_velocityClip = 0.f;
bool g_velocityClipValid = false;
// Session-wide, like the note clipboard: the velocity the next drawn note inherits.
float g_lastNoteVelocity = 100.f / 127.f;
} // namespace

bool PianoRoll::handleKeyboard(SDL_Event& e) {
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return false;

    switch (e.key.scancode) {
        case SDL_SCANCODE_C:
            if (isCtrlPressed) copySelectedNotes();
            break;
        case SDL_SCANCODE_V:
            if (isCtrlPressed) {
                // Paste on the grid/transport line nearest the playhead.
                if (const RhythmGridLine* line = closestRhythmOrTransportLine(
                        rhythmLines, transportLineCache, static_cast<float>(project->beatPosition)))
                    pasteCopiedNotes(line->integerPairs);
            }
            break;
        case SDL_SCANCODE_B:
            if (isCtrlPressed) {
                copySelectedNotes();
                pasteCopiedNotes(g_noteClip.lastEndVec); // first copied note starts at the last note's end
            }
            break;
        case SDL_SCANCODE_D:
            if (isCtrlPressed) {
                copySelectedNotes();
                pasteCopiedNotes(g_noteClip.firstStartVec); // duplicate in place
            }
            break;
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
    return true;
}

void PianoRoll::clickMouse(SDL_Event& e) {
    switch (e.type) {

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if (mouseY < topMargin && mouseX < leftMargin && gridRhythmButtonHit(mouseX, mouseY)) {
                    applyTransportToGrid();
                    return;
                }
                if(mouseY < topMargin) {
                    // Transport strip. Ctrl+drag picks two transport lines and subdivides the
                    // span between them; Ctrl+Shift rewrites the other side with that spacing.
                    if (isCtrlPressed && mouseX > leftMargin) {
                        updateTransportLines();
                        refreshHoveredTransportLineIndex();
                        selectingTransportLines = true;
                        transportLineDragExtend = isShiftPressed;
                        transportLineDragMoved = false;
                        // Start may sit on either the transport strip or the grid rhythm.
                        const float sec = static_cast<float>(scrollX - leftMargin + mouseX) / dW;
                        const RhythmGridLine* line =
                            closestRhythmOrTransportLine(rhythmLines, transportLineCache, sec);
                        if (line)
                            transportLineDragStartPairs = line->integerPairs;
                        else
                            transportLineDragStartPairs.clear();
                        transportLineDragEndPairs = transportLineDragStartPairs;
                    }
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
                    const RhythmGridLine* startLine =
                        closestRhythmOrTransportLine(rhythmLines, transportLineCache,
                                                     static_cast<float>(scrollX - leftMargin + mouseX) / dW);
                    if (rhythmIntervalStartNote) {
                        // Start on whichever edge of the note the press is nearer.
                        const float sSec = Note::beatsFromVector(rhythmIntervalStartNote->rhythmVector);
                        const float eSec = Note::beatsFromVector(rhythmIntervalStartNote->rhythmEndVector);
                        const float cursorSec = static_cast<float>(scrollX - leftMargin + mouseX) / dW;
                        if (std::fabs(eSec - cursorSec) < std::fabs(sSec - cursorSec)) {
                            rhythmIntervalStartSec = eSec;
                            rhythmDragStartVertexPairs = rhythmIntervalStartNote->rhythmEndVector;
                        } else {
                            rhythmIntervalStartSec = sSec;
                            rhythmDragStartVertexPairs = rhythmIntervalStartNote->rhythmVector;
                        }
                    } else if (startLine) {
                        rhythmIntervalStartSec = startLine->seconds;
                        rhythmDragStartVertexPairs = startLine->integerPairs;
                    } else {
                        rhythmIntervalStartSec = 0.0f;
                        rhythmDragStartVertexPairs.clear();
                    }
                    rhythmIntervalEndSec = rhythmIntervalStartSec;
                    rhythmDragEndVertexPairs.clear();
                    rhythmIntervalDragMoved = false;
                    return;
                }
                if (isShiftPressed && mouseX > leftMargin) {
                    intervalEdoDefineDialogOpen = false;
                    selectingInterval = true;
                    intervalStartNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    intervalStartLine = intervalStartNote ? intervalStartNote->num : getHoveredLine();
                    intervalEndLine = intervalStartLine;
                    intervalDragEndVertexPairs.clear();
                    if (intervalStartNote)
                        intervalDragStartVertexPairs = intervalStartNote->pitchVector;
                    else
                        intervalDragStartVertexPairs.clear();
                    intervalDragMoved = false;
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
                            g_lastNoteVelocity = n->velocity;
                            if (selectedNoteIds.count(n->id))
                                selectedNoteIds.erase(n->id);
                            else
                                selectedNoteIds.insert(n->id);
                        }
                    } else {
                        selectingRubberBand = true;
                        rubberBandStartX = mouseX;
                        rubberBandStartY = mouseY;
                        rubberBandEndX = rubberBandStartX;
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
                            auto before = captureCentralTuning();
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
                            movingNoteRhythmPreviewPairs.reset();
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
                            movingNoteRhythmPreviewPairs.reset();
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
                // V held: the right click opens the velocity menu for the hovered note.
                if (isVPressed) {
                    // Clear the right-drag flag first: handleMouse() deletes the hovered
                    // note while rmb is set and no menu is open.
                    rmb = false;
                    handleMouse();
                    openVelocityMenu();
                    refreshGrid = true;
                    break;
                }
                SDL_Log("[harmonic] rmb x=%.1f y=%.1f shift=%d ctrl=%d hovered=%p stretch=%p",
                        mouseX, mouseY, (int)isShiftPressed, (int)isCtrlPressed,
                        (void*)hoveredElement.get(), (void*)stretchingNote.get());
                if(mouseX > leftMargin && stretchingNote == nullptr) {
                    if(isCtrlPressed && isShiftPressed && hoveredElement != nullptr) {
                        auto note = std::dynamic_pointer_cast<Note>(hoveredElement);
                        if (note && region) {
                            auto before = captureCentralTuning();
                            auto after = before;
                            // Restore the note's full rhythm definition. Notes saved
                            // before gridSpans existed carry none; fall back to the
                            // region's grid they were drawn on.
                            after.gridSpans = note->gridSpans.empty() ? project->pianoRoll.gridSpans : note->gridSpans;
                            project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
                                region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
                                regionTuningSnapshotToUndoJson(after), "Recall Note Rhythm View"));
                        }
                        rmb = false;
                    }
                    else if(isShiftPressed && hoveredElement != nullptr) {
                        // Capture the note now: by the time Enter is pressed the cursor may
                        // have left the note, and hoveredElement may be null or another note.
                        std::shared_ptr<Note> target = std::dynamic_pointer_cast<Note>(hoveredElement);
                        if (!target) return;
                        const int targetID = target->id;
                        SDL_Log("[harmonic] prompt open target=%d", targetID);
                        auto ctxMenu = ContextMenu::get();
                        ctxMenu->skipNextEvent = true;
                        SDL_Window* inputWin = this->window ? this->window : project->window;
                        if (inputWin) {
                            SDL_StartTextInput(inputWin);
                            ctxMenu->activate(renderer, SDL_GetWindowID(inputWin));
                        } else {
                            ctxMenu->activate();
                        }

                        ctxMenu->dynamicTick = getTextInputTicker([this, targetID](std::string text) {
                            SDL_Log("[harmonic] enter text='%s' target=%d", text.c_str(), targetID);
                            try {
                                const int h = std::max(1, std::stoi(text));
                                if (!region) { SDL_Log("[harmonic] no region"); return; }
                                auto it = region->id_to_index.find(targetID);
                                if (it == region->id_to_index.end()) { SDL_Log("[harmonic] note gone"); return; }
                                const size_t idx = static_cast<size_t>(it->second);
                                if (idx >= region->notes.size() || !region->notes[idx]) return;
                                const std::shared_ptr<Note> note = region->notes[idx];
                                auto beforeRegion = captureCentralTuning();
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
                                SDL_Log("[harmonic] applied h=%d note=%d", h, targetID);
                                isShiftPressed = false;
                                rmb = false;
                            } catch (const std::exception& ex) {
                                SDL_Log("[harmonic] failed: %s", ex.what());
                            } catch (...) {
                                SDL_Log("[harmonic] failed: unknown");
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
                    if (movingNoteRhythmPreviewPairs) {
                        const auto startDelta = subVec(*movingNoteRhythmPreviewPairs, rhythmDragStartPairs);
                        for (auto& [nid, before] : multiMoveBefores) {
                            auto it = region->id_to_index.find(nid);
                            if (it == region->id_to_index.end()) continue;
                            const size_t idx = static_cast<size_t>(it->second);
                            if (idx >= region->notes.size() || !region->notes[idx]) continue;
                            auto& sn = region->notes[idx];
                            sn->rhythmVector = addVec(sn->rhythmVector, startDelta);
                            sn->rhythmEndVector = addVec(sn->rhythmEndVector, startDelta);
                        }
                        movingNoteRhythmPreviewPairs.reset();
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
                    if (movingNoteRhythmPreviewPairs) {
                        movingNote->rhythmVector = *movingNoteRhythmPreviewPairs;
                        const auto delta = subVec(*movingNoteRhythmPreviewPairs, rhythmDragStartPairs);
                        movingNote->rhythmEndVector = addVec(rhythmDragEndPairs, delta);
                        movingNoteRhythmPreviewPairs.reset();
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
                if (selectingTransportLines) {
                    selectingTransportLines = false;
                    SDL_Log("[transport] release moved=%d extend=%d start=%zu end=%zu",
                            (int)transportLineDragMoved, (int)transportLineDragExtend,
                            transportLineDragStartPairs.size(), transportLineDragEndPairs.size());
                    if (transportLineDragExtend) {
                        if (transportLineDragMoved)
                            extendTransportLines(transportLineDragEndPairs);
                    } else if (transportLineDragMoved) {
                        const auto aPairs = transportLineDragStartPairs;
                        const auto bPairs = transportLineDragEndPairs;
                        auto ctxMenu = ContextMenu::get();
                        ctxMenu->skipNextEvent = true;
                        SDL_Window* inputWin = this->window ? this->window : project->window;
                        if (inputWin) {
                            SDL_StartTextInput(inputWin);
                            ctxMenu->activate(renderer, SDL_GetWindowID(inputWin));
                        } else {
                            ctxMenu->activate();
                        }
                        ctxMenu->dynamicTick = getTextInputTicker(
                            [this, aPairs, bPairs](std::string text) {
                                SDL_Log("[transport] prompt text='%s'", text.c_str());
                                try {
                                    subdivideBetweenRhythmLines(aPairs, bPairs, std::max(1, std::stoi(text)));
                                } catch (...) {}
                            },
                            []() {});
                    }
                    refreshGrid = true;
                }
                if (selectingRhythmInterval) {
                    auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    const bool sameNoteClick = !rhythmIntervalDragMoved &&
                        rhythmIntervalStartNote && endNote && (rhythmIntervalStartNote->id == endNote->id);
                    selectingRhythmInterval = false;
                    rhythmEdoDefineDialogOpen = false;
                    if (sameNoteClick) {
                        auto before = captureCentralTuning();
                        auto after = before;
                        after.gridSpans = rhythmIntervalStartNote->gridSpans.empty()
                            ? project->pianoRoll.gridSpans : rhythmIntervalStartNote->gridSpans;
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
                    SDL_Window* inputWin = this->window ? this->window : project->window;
                    if (inputWin) {
                        SDL_StartTextInput(inputWin);
                        ctxMenu->activate(renderer, SDL_GetWindowID(inputWin));
                    } else {
                        ctxMenu->activate();
                    }
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
                            auto before = captureCentralTuning();
                            auto after = before;
                            after.rhythmEdoSubdivisionSteps = steps;
                            after.rhythmEdoLowerVector = lowerPairs;
                            after.rhythmEdoUpperVector = upperPairs;
                            RhythmLatticeSpan gridSpan;
                            gridSpan.steps = steps;
                            gridSpan.lower = lowerPairs;
                            gridSpan.upper = upperPairs;
                            after.gridSpans.assign(1, gridSpan);
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
                        auto before = captureCentralTuning();
                        applyNoteTuning(intervalStartNote);
                        auto after = captureCentralTuning();
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
                    SDL_Window* inputWin = this->window ? this->window : project->window;
                    if (inputWin) {
                        SDL_StartTextInput(inputWin);
                        ctxMenu->activate(renderer, SDL_GetWindowID(inputWin));
                    } else {
                        ctxMenu->activate();
                    }
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

                            auto trimmed = text;
                            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
                            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

                            auto before = captureCentralTuning();
                            auto after = before;

                            if (trimmed.empty()) {
                                // Blank + Enter: infer the interval. Rational endpoints
                                // (all exponents integers) become a harmonic grid whose
                                // lines pass exactly through both endpoints; anything
                                // with a fractional exponent is not a rational ratio, so
                                // fall back to one equal division.
                                static const int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
                                                             31, 37, 41, 43, 47, 53, 59, 61, 67, 71};
                                auto expAt = [](const std::vector<std::pair<int, int>>& v, size_t i) {
                                    if (i < v.size())
                                        return std::pair<long long, long long>{v[i].first, v[i].second ? v[i].second : 1};
                                    return std::pair<long long, long long>{0, 1};
                                };
                                bool fractional = false;
                                long long num = 1, den = 1;
                                for (size_t i = 0; i < std::size(primes) && !fractional; ++i) {
                                    auto [aN, aD] = expAt(lowerLineVec, i);
                                    auto [bN, bD] = expAt(upperLineVec, i);
                                    // upper/lower exponent difference = (bN*aD - aN*bD) / (bD*aD)
                                    const long long dn = bN * aD - aN * bD;
                                    const long long dd = bD * aD;
                                    if (dd == 0 || dn % dd != 0) {
                                        fractional = true;
                                        break;
                                    }
                                    const long long e = dn / dd;
                                    const long long p = primes[i];
                                    for (long long k = 0; k < (e > 0 ? e : -e); ++k) {
                                        if (e > 0) {
                                            if (num > (1LL << 40) / p) { fractional = true; break; }
                                            num *= p;
                                        } else {
                                            if (den > (1LL << 40) / p) { fractional = true; break; }
                                            den *= p;
                                        }
                                    }
                                }

                                if (!fractional && num <= INT32_MAX && den <= INT32_MAX) {
                                    // Anchor harmonic den at the lower pitch; the upper
                                    // endpoint then lands exactly on harmonic num.
                                    after.mode = 0;
                                    after.harmonicAnchorNumber = static_cast<int>(den);
                                    after.harmonicAnchorVector = lowerLineVec;
                                    project->um->newAction(new PianoRollRegionTuningUndoAction(project,
                                        region->parentNode->nm->managerPath, region->parentNode->id, region->id,
                                        regionTuningSnapshotToUndoJson(before), regionTuningSnapshotToUndoJson(after),
                                        "Define Harmonic Interval"));
                                    return;
                                }
                                // Simplest equal temperament: one division.
                                after.mode = 1;
                                after.edoLowerVector = lowerLineVec;
                                after.edoUpperVector = upperLineVec;
                                after.edoSubdivisionSteps = 1;
                                after.harmonicAnchorVector.clear();
                                project->um->newAction(new PianoRollRegionTuningUndoAction(project,
                                    region->parentNode->nm->managerPath, region->parentNode->id, region->id,
                                    regionTuningSnapshotToUndoJson(before), regionTuningSnapshotToUndoJson(after),
                                    "Define EDO Interval"));
                                return;
                            }

                            const int steps = std::max(1, std::stoi(trimmed));
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

        case SDL_EVENT_MOUSE_WHEEL: {
            // Hold V and scroll over a note to edit its velocity instead of scrolling.
            handleMouse();
            auto hoveredNote = std::dynamic_pointer_cast<Note>(hoveredElement);
            if (isVPressed && hoveredNote) {
                changeNoteVelocity(hoveredNote, e.wheel.y);
                break;
            }
            Scroll();
            break;
        }

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

        case SDL_EVENT_MOUSE_MOTION:

            handleMouse();

            if (selectingTransportLines) {
                refreshHoveredTransportLineIndex();
                // End snaps to the nearest line of either set (grid rhythm or transport).
                const float sec = static_cast<float>(scrollX - leftMargin + mouseX) / dW;
                if (const RhythmGridLine* line = closestRhythmOrTransportLine(rhythmLines, transportLineCache, sec)) {
                    transportLineDragEndPairs = line->integerPairs;
                    if (transportLineDragEndPairs != transportLineDragStartPairs)
                        transportLineDragMoved = true;
                }
                refreshGrid = true;
                break;
            }

            if (selectingRhythmInterval) {
                refreshHoveredRhythmLineIndex();
                // One boundary set, nearest wins, no priority: rhythm lines,
                // transport lines, and note start lines.
                const float sec = static_cast<float>(scrollX - leftMargin + mouseX) / dW;
                float bestDist = FLT_MAX;
                bool found = false;
                float bestSec = 0.f;
                std::vector<std::pair<int, int>> bestPairs;
                auto consider = [&](float s, const std::vector<std::pair<int, int>>& pairs) {
                    const float d = std::fabs(s - sec);
                    if (d < bestDist) {
                        bestDist = d;
                        bestSec = s;
                        bestPairs = pairs;
                        found = true;
                    }
                };
                for (const auto& line : rhythmLines)
                    consider(line.seconds, line.integerPairs);
                for (const auto& line : transportLineCache)
                    consider(line.seconds, line.integerPairs);
                if (region)
                    for (const auto& note : region->notes) {
                        consider(Note::beatsFromVector(note->rhythmVector), note->rhythmVector);
                        consider(Note::beatsFromVector(note->rhythmEndVector), note->rhythmEndVector);
                    }
                if (found) {
                    rhythmIntervalEndSec = bestSec;
                    rhythmDragEndVertexPairs = bestPairs;
                    if (std::fabs(rhythmIntervalEndSec - rhythmIntervalStartSec) > 0.001f)
                        rhythmIntervalDragMoved = true;
                }
                if (auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement))
                    if (!intervalStartNote || endNote->id != intervalStartNote->id)
                        rhythmIntervalDragMoved = true;
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
                                if (gridHasLineAt(rhythmLines, sn->rhythmVector))
                                    sn->gridSpans = project->pianoRoll.gridSpans;
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
            note->tuningEdoSubdivisionSteps = project->pianoRoll.tuningEdoSubdivisionSteps;
            note->tuningEdoLowerVector = project->pianoRoll.tuningEdoLowerVector;
            note->tuningEdoUpperVector = project->pianoRoll.tuningEdoUpperVector;
        }
        refreshHoveredRhythmLineIndex();
        const size_t rli = hoveredRhythmLineIndex;
        if (rli != SIZE_MAX && rli < rhythmLines.size())
            note->rhythmVector = rhythmLines[rli].integerPairs;
        note->gridSpans = project->pianoRoll.gridSpans; // whole rhythm view the note was created with
        note->velocity = g_lastNoteVelocity;             // inherit the last touched velocity
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
    if (selectedNoteIds.empty() || !region)
        return;
    std::vector<int> toDelete(selectedNoteIds.begin(), selectedNoteIds.end());
    selectedNoteIds.clear();
    hoveredElement = nullptr;
    // One undo step for the whole selection.
    project->um->newAction(new DeleteNotesAction(project, region->parentNode->nm->managerPath,
        static_cast<int>(region->parentNode->id), static_cast<int>(region->id), std::move(toDelete)));
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
    // Pick the closest note edge under the cursor, not the first note in the
    // region whose edge zone happens to overlap it.
    std::shared_ptr<Note> best;
    int bestDir = 0;
    float bestDist = 1e9f;
    for (std::shared_ptr<Note> note : region->notes) {
        if (note->channel != currentChannel) continue;

        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(noteMidiForRender(note));

        if (mouseY >= noteY - noteRadius && mouseY <= noteY + noteRadius) {
            const float edgeZone = std::min(noteRadius, (noteEnd - notePosX) * 0.3f);
            const float dStart = std::fabs(mouseX - static_cast<float>(notePosX));
            const float dEnd = std::fabs(mouseX - static_cast<float>(noteEnd));
            if (dStart <= edgeZone && dStart < bestDist) {
                bestDist = dStart;
                best = note;
                bestDir = -1;
            }
            if (dEnd <= edgeZone && dEnd < bestDist) {
                bestDist = dEnd;
                best = note;
                bestDir = 1;
            }
        }
    }
    if (best) {
        stretchingNote = best;
        resizeDir = bestDir;
        isStretchingNote = true;
        return true;
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
    // Resize edges snap to the shared boundary set (rhythm, transport, other notes).
    TimeBoundary boundary;
    if (closestTimeBoundary(rhythmLines, transportLineCache, region ? &region->notes : nullptr,
                            stretchingNote->id,
                            static_cast<float>(scrollX - leftMargin + mouseX) / dW, boundary)) {
        const float lineSec = boundary.seconds;
        if (resizeDir == -1 && lineSec < stretchingNote->endBeats() - 0.001f) {
            stretchingNote->rhythmVector = boundary.pairs;
            // Re-anchor the note onto this line: adopt the grid's rhythm
            // temperament only if that grid has a line at the note's start.
            if (region && gridHasLineAt(rhythmLines, stretchingNote->rhythmVector)) {
                stretchingNote->gridSpans = project->pianoRoll.gridSpans;
            }
        } else if (resizeDir == 1 && lineSec > stretchingNote->startBeats() + 0.001f) {
            stretchingNote->rhythmEndVector = boundary.pairs;
            if (region && gridHasLineAt(rhythmLines, stretchingNote->rhythmVector)) {
                stretchingNote->gridSpans = project->pianoRoll.gridSpans;
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
    // Snap to the shared boundary set: rhythm lines, transport lines, other notes.
    TimeBoundary boundary;
    if (closestTimeBoundary(rhythmLines, transportLineCache, region ? &region->notes : nullptr,
                            note->id,
                            static_cast<float>(scrollX - leftMargin + mouseX) / dW, boundary))
        movingNoteRhythmPreviewPairs = boundary.pairs;
    mouseX += rhythmDragGrabOffsetPx;
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
            note->tuningEdoSubdivisionSteps = project->pianoRoll.tuningEdoSubdivisionSteps;
            note->tuningEdoLowerVector = project->pianoRoll.tuningEdoLowerVector;
            note->tuningEdoUpperVector = project->pianoRoll.tuningEdoUpperVector;
        }
        movingNoteDragDirty = true;
    }
    stampNoteTuning(note);
    Scroll();
}

void PianoRoll::snapNoteRhythm(const std::shared_ptr<Note>& note) {
    if (!note || !region || rhythmLines.empty()) return;
    const size_t rli = closestRhythmLineIndexForSeconds(note->startBeats());
    if (rli != SIZE_MAX && rli < rhythmLines.size())
        note->rhythmVector = rhythmLines[rli].integerPairs;
}

bool PianoRoll::hasNoteClipboard() const { return g_noteClip.valid; }

float PianoRoll::snapXToBoundary(float x) const {
    const float sec = static_cast<float>((static_cast<double>(x) + static_cast<double>(scrollX) -
                                          static_cast<double>(leftMargin)) / dW);
    // Same boundary set as the rhythm-interval drag: rhythm lines, transport
    // lines, note starts; nearest wins, no priority.
    float bestDist = FLT_MAX;
    float bestSec = sec;
    if (const RhythmGridLine* line = closestRhythmOrTransportLine(rhythmLines, transportLineCache, sec))
        bestDist = std::fabs(line->seconds - sec), bestSec = line->seconds;
    if (region) {
        for (const auto& note : region->notes) {
            const float s = Note::beatsFromVector(note->rhythmVector);
            const float e = Note::beatsFromVector(note->rhythmEndVector);
            const float ds = std::fabs(s - sec);
            const float de = std::fabs(e - sec);
            if (ds < bestDist) { bestDist = ds; bestSec = s; }
            if (de < bestDist) { bestDist = de; bestSec = e; }
        }
    }
    if (!(bestDist < FLT_MAX))
        return x;
    return static_cast<float>(static_cast<double>(bestSec) * dW - static_cast<double>(scrollX) +
                              static_cast<double>(leftMargin));
}

std::vector<int> PianoRoll::velocityTargetIDs() const {
    std::vector<int> ids;
    if (region) {
        for (const auto& n : region->notes)
            if (n && n->channel == currentChannel && selectedNoteIds.count(n->id))
                ids.push_back(n->id);
    }
    if (auto n = std::dynamic_pointer_cast<Note>(hoveredElement))
        if (std::find(ids.begin(), ids.end(), n->id) == ids.end())
            ids.push_back(n->id);
    return ids;
}

void PianoRoll::applyNoteVelocity(const std::vector<int>& noteIDs, bool relative, float amount) {
    if (!region || noteIDs.empty()) return;
    std::vector<int> ids;
    std::vector<json> befores, afters;
    for (int id : noteIDs) {
        auto it = region->id_to_index.find(id);
        if (it == region->id_to_index.end()) continue;
        const size_t idx = static_cast<size_t>(it->second);
        if (idx >= region->notes.size() || !region->notes[idx]) continue;
        auto& n = region->notes[idx];
        json before = n->toJSON();
        n->velocity = std::clamp(relative ? n->velocity + amount : amount, 0.f, 1.f);
        json after = n->toJSON();
        if (after == before) continue;
        g_lastNoteVelocity = n->velocity;
        ids.push_back(id);
        befores.push_back(std::move(before));
        afters.push_back(std::move(after));
    }
    if (ids.empty()) return;
    Node* parent = region->parentNode;
    if (ids.size() == 1)
        project->um->newAction(new MoveNoteAction(project, parent->nm->managerPath, parent->id,
            region->id, ids[0], std::move(befores[0]), std::move(afters[0]), "Note Velocity"));
    else
        project->um->newAction(new MoveMultipleNotesAction(project, parent->nm->managerPath, parent->id,
            region->id, std::move(ids), std::move(befores), std::move(afters), "Note Velocity"));
}

void PianoRoll::changeNoteVelocity(const std::shared_ptr<Note>& note, float ticks) {
    if (!region || !note || ticks == 0.f) return;
    // Same delta for every selected note, plus the hovered note.
    applyNoteVelocity(velocityTargetIDs(), true, ticks * 0.05f);
}

void PianoRoll::openVelocityMenu() {
    auto note = std::dynamic_pointer_cast<Note>(hoveredElement);
    if (!note || !region) return;
    const std::vector<int> ids = velocityTargetIDs();
    const float value = note->velocity;

    auto ctxMenu = ContextMenu::get();
    ctxMenu->skipNextEvent = true;
    SDL_Window* inputWin = this->window ? this->window : project->window;
    if (inputWin) {
        SDL_StartTextInput(inputWin);
        ctxMenu->activate(renderer, SDL_GetWindowID(inputWin));
    } else {
        ctxMenu->activate();
    }

    char label[64];
    std::snprintf(label, sizeof(label), "Velocity %.2f (%d)", value,
                  static_cast<int>(std::lround(value * 127.f)));
    auto root = std::make_shared<TreeEntry>(label);

    auto setEntry = std::make_shared<TreeEntry>("Set Velocity...");
    setEntry->click = [this, ids, inputWin, ctxMenu]() {
        ctxMenu->skipNextEvent = true;
        if (inputWin)
            ctxMenu->activate(renderer, SDL_GetWindowID(inputWin));
        else
            ctxMenu->activate();
        ctxMenu->dynamicTick = getTextInputTicker([this, ids](std::string text) {
            try {
                float v = std::stof(text);
                if (v > 1.f) v /= 127.f; // accept 0..127 as well
                applyNoteVelocity(ids, false, v);
            } catch (...) {
            }
        }, []() {});
    };
    root->addChild(setEntry);

    auto copyEntry = std::make_shared<TreeEntry>("Copy Velocity");
    copyEntry->click = [note]() {
        g_velocityClip = note->velocity;
        g_velocityClipValid = true;
    };
    root->addChild(copyEntry);

    if (g_velocityClipValid) {
        const float clip = g_velocityClip;
        auto pasteEntry = std::make_shared<TreeEntry>("Paste Velocity");
        pasteEntry->click = [this, ids, clip]() { applyNoteVelocity(ids, false, clip); };
        root->addChild(pasteEntry);
    }

    auto resetEntry = std::make_shared<TreeEntry>("Reset to Default");
    resetEntry->click = [this, ids]() { applyNoteVelocity(ids, false, 100.f / 127.f); };
    root->addChild(resetEntry);

    ctxMenu->dynamicTick = getTreeMenuTicker(root);
    rmb = false;
}

void PianoRoll::copySelectedNotes() {
    if (!region) return;
    std::vector<std::shared_ptr<Note>> src;
    for (int nid : selectedNoteIds) {
        auto it = region->id_to_index.find(nid);
        if (it == region->id_to_index.end()) continue;
        const size_t idx = static_cast<size_t>(it->second);
        if (idx < region->notes.size() && region->notes[idx]) src.push_back(region->notes[idx]);
    }
    if (src.empty()) {
        if (auto n = std::dynamic_pointer_cast<Note>(hoveredElement)) src.push_back(n);
    }
    if (src.empty()) return;

    g_noteClip.notes.clear();
    g_noteClip.firstStartVec.clear();
    g_noteClip.lastEndVec.clear();
    float first = FLT_MAX;
    float last = -FLT_MAX;
    for (const auto& n : src) {
        g_noteClip.notes.push_back(n->toJSON());
        const float startSec = n->startBeats();
        const float endSec = n->endBeats();
        if (startSec < first) {
            first = startSec;
            g_noteClip.firstStartVec = n->rhythmVector;
        }
        if (endSec > last) {
            last = endSec;
            g_noteClip.lastEndVec = n->rhythmEndVector;
        }
    }
    g_noteClip.valid = true;
}

void PianoRoll::pasteCopiedNotes(const std::vector<std::pair<int, int>>& targetStart) {
    if (!region || !g_noteClip.valid || g_noteClip.notes.empty()) return;

    // Shift the whole set so its earliest note lands on targetStart. Both ends are exact
    // vectors, so the shift is pure vector arithmetic — no beat value is re-quantized.
    const auto shift = subVec(targetStart, g_noteClip.firstStartVec);

    std::vector<CreateNoteSpec> specs;
    specs.reserve(g_noteClip.notes.size());
    for (const auto& j : g_noteClip.notes) {
        CreateNoteSpec s;
        s.pitchVector = pairsFromJSON(j.at("pitchVector"));
        s.rhythmVector = addVec(pairsFromJSON(j.at("rhythmVector")), shift);
        s.rhythmEndVector = addVec(pairsFromJSON(j.at("rhythmEndVector")), shift);
        // createNote stamps tuning from the current view; re-apply the copied note's own
        // channel, tuning fields and rhythm definition (gridSpans) over that stamp.
        s.noteStampedSnapshot = j;
        s.noteStampedSnapshot["rhythmVector"] = pairsToJSON(s.rhythmVector);
        s.noteStampedSnapshot["rhythmEndVector"] = pairsToJSON(s.rhythmEndVector);
        specs.push_back(std::move(s));
    }

    project->um->newAction(new CreateNotesAction(project, region->parentNode->nm->managerPath,
        region->parentNode->id, region->id, std::move(specs)));
    refreshGrid = true;
}

// ---------------------------------------------------------------------------
// Ctrl+drag on the transport strip. The strip's lines come from the region's
// transport line set — a list of {lower, upper, steps} spans covering the
// timeline. Dragging between two lines and typing N replaces the span between
// them with one of N parts, so only that range changes spacing. Ctrl+Shift
// writes the current spacing into the neighbouring interval instead.
// ---------------------------------------------------------------------------

void PianoRoll::updateTransportLines() {
    transportLineCache.clear();
    if (!region) return;

    if (project->pianoRoll.transportLines.empty())
        project->pianoRoll.transportLines.push_back(defaultLatticeSpan(1));

    const float minSec = (static_cast<float>(scrollX) - leftMargin) / static_cast<float>(dW);
    const float maxSec = (static_cast<float>(scrollX) + width - leftMargin) / static_cast<float>(dW);
    std::vector<std::string> labels;
    generateTransportLines(transportLineCache, labels, project->pianoRoll.transportLines, minSec, maxSec);
}

size_t PianoRoll::closestTransportLineIndexForSeconds(float seconds) {
    return ::closestRhythmLineIndexForSeconds(transportLineCache, seconds);
}

void PianoRoll::refreshHoveredTransportLineIndex() {
    hoveredTransportLineIndex = SIZE_MAX;
    if (transportLineCache.empty()) return;
    if (mouseX < leftMargin) return;
    const float sec = (mouseX + scrollX - leftMargin) / dW;
    hoveredTransportLineIndex = closestTransportLineIndexForSeconds(sec);
}

void PianoRoll::applyTransportToGrid() {
    if (!region || project->pianoRoll.transportLines.empty()) return;
    auto before = captureCentralTuning();
    if (project->pianoRoll.transportLines == before.gridSpans) return;
    auto after = before;
    after.gridSpans = project->pianoRoll.transportLines;
    project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
        region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
        regionTuningSnapshotToUndoJson(after), "Grid From Transport"));
    refreshGrid = true;
}

void PianoRoll::subdivideBetweenRhythmLines(const std::vector<std::pair<int, int>>& aPairs,
                                            const std::vector<std::pair<int, int>>& bPairs,
                                            int parts) {
    if (!region || parts < 1) return;

    // The two dragged lines become the bounds of a new span with `parts` divisions, which
    // replaces whatever spans covered that range.
    const bool aFirst = Note::beatsFromVector(aPairs) <= Note::beatsFromVector(bPairs);
    RhythmLatticeSpan span;
    span.steps = parts;
    span.lower = aFirst ? aPairs : bPairs;
    span.upper = aFirst ? bPairs : aPairs;

    auto before = captureCentralTuning();
    auto after = before;
    insertLatticeSpan(after.transportLines, span);

    if (after.transportLines == before.transportLines)
        return;

    project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
        region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
        regionTuningSnapshotToUndoJson(after), "Subdivide Rhythm Lines"));
    refreshGrid = true;
}

void PianoRoll::extendTransportLines(const std::vector<std::pair<int, int>>& cursorPairs) {
    if (!region) return;
    const float cursorSec = Note::beatsFromVector(cursorPairs);

    // Spacing comes from the span the cursor sits in.
    int sourceSteps = std::max(1, project->pianoRoll.rhythmEdoSubdivisionSteps);
    for (const auto& s : project->pianoRoll.transportLines) {
        if (Note::beatsFromVector(s.lower) <= cursorSec && cursorSec <= Note::beatsFromVector(s.upper)) {
            sourceSteps = s.steps;
            break;
        }
    }

    // Write that spacing into the larger neighbouring interval, up to the beat line.
    const RhythmGridLine* prevBeat = nullptr;
    const RhythmGridLine* nextBeat = nullptr;
    for (const auto& rl : rhythmLines) {
        if (rl.seconds < cursorSec &&
            (!prevBeat || rl.seconds > prevBeat->seconds)) prevBeat = &rl;
        if (rl.seconds > cursorSec &&
            (!nextBeat || rl.seconds < nextBeat->seconds)) nextBeat = &rl;
    }
    const float leftSpan = prevBeat ? cursorSec - prevBeat->seconds : -1.f;
    const float rightSpan = nextBeat ? nextBeat->seconds - cursorSec : -1.f;
    const bool useLeft = (leftSpan >= rightSpan);
    if (useLeft ? !prevBeat : !nextBeat) return;

    RhythmLatticeSpan span;
    span.steps = sourceSteps;
    span.lower = useLeft ? prevBeat->integerPairs : cursorPairs;
    span.upper = useLeft ? cursorPairs : nextBeat->integerPairs;

    auto before = captureCentralTuning();
    auto after = before;
    insertLatticeSpan(after.transportLines, span);

    if (after.transportLines == before.transportLines)
        return;

    project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
        region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
        regionTuningSnapshotToUndoJson(after), "Extend Rhythm Lines"));
    refreshGrid = true;
}

