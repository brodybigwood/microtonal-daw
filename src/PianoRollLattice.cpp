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

// Tuning model + pitch/rhythm lattice lines: apply/stamp note tuning, EDO and
// harmonic line generation, grid hover resolution, tuning-undo notification
// (split from PianoRoll.cpp).

static constexpr Uint64 kPitchFactorsTooltipDwellMs = 500;

void PianoRoll::refreshPitchFactorsHoverTiming() {
    if (!hoveredElement) {
        hoverPitchFactorsNoteId = -1;
        return;
    }
    if (hoverPitchFactorsNoteId != hoveredElement->id) {
        hoverPitchFactorsNoteId = hoveredElement->id;
        hoverPitchFactorsStartMs = SDL_GetTicks();
    }
}

void PianoRoll::renderPitchFactorsHoverTooltip() {
    auto note = hoveredElement;
    if (!note || !fonts.mainFont)
        return;
    if (SDL_GetTicks() - hoverPitchFactorsStartMs < kPitchFactorsTooltipDwellMs)
        return;
    if (hoverPitchFactorsNoteId != note->id)
        return;

    const std::string text = formatPrimePowerVector(note->pitchIntegerPairs);
    const SDL_FRect bounds{0.f, 0.f, static_cast<float>(width), static_cast<float>(height - bottomMargin)};
    renderTooltip(renderer, text, mouseX + dstRect->x, mouseY + dstRect->y, bounds);
}



void PianoRoll::applyNoteTuning(const std::shared_ptr<Note>& note) {
    if (!note) return;
    tuningMode = (note->tuningMode == 1) ? TuningMode::EDO : TuningMode::Harmonic;
    harmonicAnchorNumber = std::max(1, note->tuningAnchorHarmonic);
    if (region) {
        if (tuningMode == TuningMode::Harmonic) {
            region->tuningHarmonicAnchorVector = note->pitchIntegerPairs;
        } else {
            region->tuningHarmonicAnchorVector.clear();
            region->tuningEdoSubdivisionSteps = note->tuningEdoSubdivisionSteps;
            region->tuningEdoLowerVector = note->tuningEdoLowerVector;
            region->tuningEdoUpperVector = note->tuningEdoUpperVector;
        }
    }
    syncTuningToRegion();
    updateLines();
    updateRhythmLines();
}

void PianoRoll::stampNoteTuning(const std::shared_ptr<Note>& note) {
    if (!note) return;
    note->tuningMode = (tuningMode == TuningMode::EDO) ? 1 : 0;
    note->syncNumFromPitchIntegerPairs();
}

void PianoRoll::syncTuningToRegion() {
    if (!region) return;
    region->tuningMode = static_cast<int>(tuningMode);
    region->tuningAnchorHarmonic = harmonicAnchorNumber;
}

void PianoRoll::loadTuningFromRegion() {
    if (!region) return;
    tuningMode = (region->tuningMode == 1) ? TuningMode::EDO : TuningMode::Harmonic;
    harmonicAnchorNumber = std::max(1, region->tuningAnchorHarmonic);
}

void PianoRoll::newTuning() {
    // Toggle harmonic/EDO view; keep both lattices (harmonic anchor vector + EDO span/vectors) across toggles.
    auto before = captureRegionTuning(region);
    auto after = before;
    after.mode = (before.mode == 0) ? 1 : 0;
    project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath, region->parentNode->id,
        region->id, regionTuningSnapshotToUndoJson(before), regionTuningSnapshotToUndoJson(after), "Toggle Tuning Mode"));
}

void PianoRoll::updateLines() {
    // Snap tuningMode / anchors from region before building pitchLines so snapping never uses a stale lattice.
    loadTuningFromRegion();

    pitchLines.clear();
    lineLabels.clear();
    lineStructural.clear();

    if (tuningMode == TuningMode::Harmonic) {
        const auto& anchorVec = region->tuningHarmonicAnchorVector;
        const auto anchorBase = subVec(anchorVec, densePrimeExponentPairsForHarmonic(harmonicAnchorNumber));
        for (int h = 1; h <= 512; ++h) {
            auto pairs = addVec(anchorBase, densePrimeExponentPairsForHarmonic(h));
            const float midi = Note::midiFromPitchIntegerPairs(pairs);
            if (midi < -24.0f || midi > 152.0f) continue;
            pitchLines.emplace_back(midi);
            pitchLines.back().integerPairs = std::move(pairs);
            lineLabels.push_back(std::to_string(h));
            lineStructural.push_back(h);
        }
    } else {
        const int subdiv = region ? region->tuningEdoSubdivisionSteps : 0;
        if (region && subdiv > 0) {
            for (int k = -1024; k <= 1024; ++k) {
                std::vector<std::pair<int, int>> pairs =
                    edoVectorForK(k, subdiv, region->tuningEdoLowerVector, region->tuningEdoUpperVector);
                const float midi = Note::midiFromPitchIntegerPairs(pairs);
                if (midi < -24.0f || midi > 152.0f) continue;
                pitchLines.emplace_back(midi);
                pitchLines.back().integerPairs = std::move(pairs);
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(2) << midi;
                lineLabels.push_back(ss.str());
                lineStructural.push_back(k);
            }
        }
    }
    Scroll();
}

void PianoRoll::updateRhythmLines() {
    const int steps = region ? region->rhythmEdoSubdivisionSteps : 1;
    static const std::vector<std::pair<int,int>> kOneSec{{1,1}};
    const auto& lower = region ? region->rhythmEdoLowerVector : std::vector<std::pair<int,int>>{};
    const auto& upper = (region && !region->rhythmEdoUpperVector.empty())
        ? region->rhythmEdoUpperVector : kOneSec;
    generateRhythmLines(rhythmLines, rhythmLineLabels, steps, lower, upper);
}

size_t PianoRoll::closestRhythmLineIndexForSeconds(float seconds) {
    return ::closestRhythmLineIndexForSeconds(rhythmLines, seconds);
}

void PianoRoll::refreshHoveredRhythmLineIndex() {
    hoveredRhythmLineIndex = SIZE_MAX;
    if (rhythmLines.empty()) return;
    if (mouseX < leftMargin) return;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < rhythmLines.size(); ++i) {
        const float d = std::fabs(mouseX - getX(rhythmLines[i].seconds));
        if (d < bd) { bd = d; best = i; }
    }
    hoveredRhythmLineIndex = best;
}

size_t PianoRoll::closestLineIndexForMidi(float midiPitch) const {
    if (pitchLines.empty())
        return SIZE_MAX;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < pitchLines.size(); ++i) {
        const float d = std::fabs(pitchLines[i].midi - midiPitch);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

std::vector<std::pair<int, int>> PianoRoll::pitchIntegerPairsAtGridMidi(float midiPitch) const {
    const size_t li = closestLineIndexForMidi(midiPitch);
    if (li != SIZE_MAX && li < pitchLines.size())
        return pitchLines[li].integerPairs;
    return {};
}

void PianoRoll::refreshHoveredPitchLineIndex() {
    hoveredPitchLineIndex = SIZE_MAX;
    if (pitchLines.empty())
        return;
    if (mouseX < leftMargin || mouseY < topMargin)
        return;
    if (mouseY > height - bottomMargin)
        return;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < pitchLines.size(); ++i) {
        const float d = std::fabs(mouseY - getY(pitchLines[i].midi));
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    hoveredPitchLineIndex = best;
}

int PianoRoll::hoveredHarmonicFromGrid() {
    if (tuningMode != TuningMode::Harmonic || lineLabels.empty() || pitchLines.empty())
        return 0;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < pitchLines.size(); ++i) {
        const float d = std::fabs(mouseY - getY(pitchLines[i].midi));
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    try {
        return std::max(1, std::stoi(lineLabels.at(best)));
    } catch (...) {
        return 0;
    }
}

int PianoRoll::hoveredEdoKFromGrid() {
    if (tuningMode != TuningMode::EDO || lineStructural.empty() || pitchLines.empty())
        return INT_MAX;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < pitchLines.size(); ++i) {
        const float d = std::fabs(mouseY - getY(pitchLines[i].midi));
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return lineStructural[best];
}

int PianoRoll::structuralHarmonicNearNote(const std::shared_ptr<Note>& n) {
    if (!n)
        return hoveredHarmonicFromGrid();
    if (n->tuningAnchorHarmonic > 0) {
        for (size_t i = 0; i < lineStructural.size(); ++i) {
            if (lineStructural[i] == n->tuningAnchorHarmonic)
                return std::max(1, n->tuningAnchorHarmonic);
        }
    }
    if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lineStructural.size())
        return std::max(1, lineStructural[hoveredPitchLineIndex]);
    return hoveredHarmonicFromGrid();
}

int PianoRoll::structuralEdoKNearNote(const std::shared_ptr<Note>& n) {
    if (!n)
        return hoveredEdoKFromGrid();
    const size_t li = closestLineIndexForMidi(n->num);
    if (li != SIZE_MAX && li < lineStructural.size())
        return lineStructural[li];
    if (hoveredPitchLineIndex != SIZE_MAX && hoveredPitchLineIndex < lineStructural.size())
        return lineStructural[hoveredPitchLineIndex];
    return hoveredEdoKFromGrid();
}

void PianoRoll::notifyTuningUndoApplied(Project* p, const std::vector<int>& managerPath, int arrangerNodeId, int regionId,
                                        int noteIdToStamp) {
    ArrangerNode* arr = undoResolveArrangerNode(p, managerPath, arrangerNodeId);
    if (!arr || !arr->sl)
        return;
    PianoRoll* pr = nullptr;
    for (auto* candidate : arr->sl->pianoRolls) {
        if (candidate->region && static_cast<int>(candidate->region->id) == regionId) {
            pr = candidate;
            break;
        }
    }
    if (!pr) return;
    pr->updateLines();
    pr->updateRhythmLines();
    pr->refreshGrid = true;
    if (noteIdToStamp < 0)
        return;
    Region* reg = undoResolveArrangerRegion(p, managerPath, arrangerNodeId, regionId);
    if (!reg)
        return;
    auto it = reg->id_to_index.find(noteIdToStamp);
    if (it == reg->id_to_index.end())
        return;
    const size_t idx = static_cast<size_t>(it->second);
    if (idx >= reg->notes.size())
        return;
    const std::shared_ptr<Note>& note = reg->notes[idx];
    if (note)
        pr->stampNoteTuning(note);
}

