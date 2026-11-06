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

    const std::string text = formatPrimePowerVector(note->pitchVector);
    const SDL_FRect bounds{0.f, 0.f, static_cast<float>(width), static_cast<float>(height - bottomMargin)};
    renderTooltip(renderer, text, mouseX + dstRect->x, mouseY + dstRect->y, bounds);
}



RegionTuningSnapshot PianoRoll::captureCentralTuning() const {
    RegionTuningSnapshot s;
    if (!project) return s;
    const PianoRollSettings& pr = project->pianoRoll;
    s.mode = pr.tuningMode;
    s.harmonicAnchorNumber = pr.tuningAnchorHarmonic;
    s.edoSubdivisionSteps = pr.tuningEdoSubdivisionSteps;
    s.edoLowerVector = pr.tuningEdoLowerVector;
    s.edoUpperVector = pr.tuningEdoUpperVector;
    s.harmonicAnchorVector = pr.tuningHarmonicAnchorVector;
    s.rhythmEdoSubdivisionSteps = pr.rhythmEdoSubdivisionSteps;
    s.rhythmEdoLowerVector = pr.rhythmEdoLowerVector;
    s.rhythmEdoUpperVector = pr.rhythmEdoUpperVector;
    s.transportLines = pr.transportLines;
    s.gridSpans = pr.gridSpans;
    return s;
}

void PianoRoll::applyCentralTuning(const RegionTuningSnapshot& s) {
    if (!project) return;
    PianoRollSettings& pr = project->pianoRoll;
    pr.tuningMode = s.mode;
    pr.tuningAnchorHarmonic = s.harmonicAnchorNumber;
    pr.tuningEdoSubdivisionSteps = std::max(1, s.edoSubdivisionSteps);
    pr.tuningEdoLowerVector = s.edoLowerVector;
    pr.tuningEdoUpperVector = s.edoUpperVector;
    pr.tuningHarmonicAnchorVector = s.harmonicAnchorVector;
    pr.rhythmEdoSubdivisionSteps = s.rhythmEdoSubdivisionSteps;
    pr.rhythmEdoLowerVector = s.rhythmEdoLowerVector;
    pr.rhythmEdoUpperVector = s.rhythmEdoUpperVector;
    pr.transportLines = s.transportLines;
    pr.gridSpans = s.gridSpans;
    if (pr.gridSpans.empty()) pr.gridSpans.push_back(RhythmLatticeSpan{});
}

void PianoRoll::applyNoteTuning(const std::shared_ptr<Note>& note) {
    if (!note) return;
    tuningMode = (note->tuningMode == 1) ? TuningMode::EDO : TuningMode::Harmonic;
    harmonicAnchorNumber = std::max(1, note->tuningAnchorHarmonic);
    if (project) {
        PianoRollSettings& pr = project->pianoRoll;
        if (tuningMode == TuningMode::Harmonic) {
            pr.tuningHarmonicAnchorVector = note->pitchVector;
        } else {
            pr.tuningHarmonicAnchorVector.clear();
            pr.tuningEdoSubdivisionSteps = note->tuningEdoSubdivisionSteps;
            pr.tuningEdoLowerVector = note->tuningEdoLowerVector;
            pr.tuningEdoUpperVector = note->tuningEdoUpperVector;
        }
    }
    syncTuningToRegion();
    updateLines();
    updateRhythmLines();
}

void PianoRoll::stampNoteTuning(const std::shared_ptr<Note>& note) {
    if (!note) return;
    note->tuningMode = (tuningMode == TuningMode::EDO) ? 1 : 0;
    note->syncNumFromPitchVector();
}

void PianoRoll::syncTuningToRegion() {
    if (!project) return;
    project->pianoRoll.tuningMode = static_cast<int>(tuningMode);
    project->pianoRoll.tuningAnchorHarmonic = harmonicAnchorNumber;
}

void PianoRoll::loadTuningFromRegion() {
    if (!project) return;
    tuningMode = (project->pianoRoll.tuningMode == 1) ? TuningMode::EDO : TuningMode::Harmonic;
    harmonicAnchorNumber = std::max(1, project->pianoRoll.tuningAnchorHarmonic);
}

void PianoRoll::newTuning() {
    // Toggle harmonic/EDO view; keep both lattices (harmonic anchor vector + EDO span/vectors) across toggles.
    auto before = captureCentralTuning();
    auto after = before;
    after.mode = (before.mode == 0) ? 1 : 0;
    project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath, region->parentNode->id,
        region ? region->id : -1, regionTuningSnapshotToUndoJson(before), regionTuningSnapshotToUndoJson(after), "Toggle Tuning Mode"));
}

void PianoRoll::updateLines() {
    // Snap tuningMode / anchors from region before building pitchLines so snapping never uses a stale lattice.
    loadTuningFromRegion();

    pitchLines.clear();
    lineLabels.clear();
    lineStructural.clear();

    if (tuningMode == TuningMode::Harmonic) {
        const auto& anchorVec = project ? project->pianoRoll.tuningHarmonicAnchorVector
                                        : std::vector<std::pair<int, int>>{};
        const auto anchorBase = subVec(anchorVec, densePrimeExponentPairsForHarmonic(harmonicAnchorNumber));
        for (int h = 1; h <= 512; ++h) {
            auto pairs = addVec(anchorBase, densePrimeExponentPairsForHarmonic(h));
            const float midi = Note::midiFromPitchVector(pairs);
            if (midi < -24.0f || midi > 152.0f) continue;
            pitchLines.emplace_back(midi);
            pitchLines.back().integerPairs = std::move(pairs);
            lineLabels.push_back(std::to_string(h));
            lineStructural.push_back(h);
        }
    } else {
        const int subdiv = project ? project->pianoRoll.tuningEdoSubdivisionSteps : 0;
        if (project && subdiv > 0) {
            for (int k = -1024; k <= 1024; ++k) {
                std::vector<std::pair<int, int>> pairs =
                    edoVectorForK(k, subdiv, project->pianoRoll.tuningEdoLowerVector,
                                  project->pianoRoll.tuningEdoUpperVector);
                const float midi = Note::midiFromPitchVector(pairs);
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
    const int steps = project ? project->pianoRoll.rhythmEdoSubdivisionSteps : 1;
    static const std::vector<std::pair<int,int>> kOneSec{{1,1}};
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

std::vector<std::pair<int, int>> PianoRoll::pitchVectorAtGridMidi(float midiPitch) const {
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

