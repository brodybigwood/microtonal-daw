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

namespace {
struct RegionTuningSnapshot {
    int mode = 0;
    float harmonicAnchorMidi = 69.0f;
    int harmonicAnchorNumber = 1;
    float edoAnchorMidi = 69.0f;
    float edoStep = 1.0f;
    int edoSpanDivisions = 0;
    float edoSpanLoMidi = 0.0f;
    float edoSpanHiMidi = 0.0f;
    int spanLoHarm = 0;
    int spanHiHarm = 0;
    int spanLoEdoK = INT_MAX;
    int spanHiEdoK = INT_MAX;
    int edoStepSemiNum = 1;
    int edoStepSemiDen = 1;
    int edoSubdivisionSteps = 12;
    std::vector<std::pair<int, int>> edoLowerVector;
    std::vector<std::pair<int, int>> edoUpperVector;
    std::vector<std::pair<int, int>> harmonicAnchorVector;
};

struct NoteTuningSnapshot {
    int harmonicNumber = 0;
    int tuningMode = 0;
    float tuningAnchorMidi = 69.0f;
    int tuningAnchorHarmonic = 1;
    float tuningEdoAnchorMidi = 69.0f;
    float tuningEdoStep = 1.0f;
};

static bool isPrimeInt(int p) {
    if (p < 2)
        return false;
    for (int d = 2; d * d <= p; ++d) {
        if (p % d == 0)
            return false;
    }
    return true;
}

static std::vector<int> primesUpToInclusive(int maxP) {
    std::vector<int> out;
    for (int p = 2; p <= maxP; ++p) {
        if (isPrimeInt(p))
            out.push_back(p);
    }
    return out;
}

static int largestPrimeFactor(int h) {
    if (h < 2)
        return 1;
    int n = h;
    int g = 1;
    for (int p = 2; p * p <= n; ++p) {
        if (n % p != 0)
            continue;
        while (n % p == 0)
            n /= p;
        g = std::max(g, p);
    }
    if (n > 1)
        g = std::max(g, n);
    return g;
}

// One (numerator, denominator) per prime 2,3,5,… up to largest prime dividing h; exponent 0 when p ∤ h.
static std::vector<std::pair<int, int>> densePrimeExponentPairsForHarmonic(int h) {
    std::vector<std::pair<int, int>> out;
    if (h < 2)
        return out;
    const int maxP = largestPrimeFactor(h);
    for (int p : primesUpToInclusive(maxP)) {
        int n = h;
        int e = 0;
        while (n % p == 0) {
            n /= p;
            ++e;
        }
        out.push_back({e, 1});
    }
    return out;
}

static std::pair<int, int> ratNorm(long long num, long long den) {
    if (den == 0)
        return {0, 1};
    if (den < 0) {
        num = -num;
        den = -den;
    }
    if (num == 0)
        return {0, 1};
    long long g = std::gcd(num, den);
    num /= g;
    den /= g;
    return {static_cast<int>(num), static_cast<int>(den)};
}

static std::pair<int, int> ratSub(std::pair<int, int> a, std::pair<int, int> b) {
    const long long n = (long long)a.first * b.second - (long long)b.first * a.second;
    const long long d = (long long)a.second * b.second;
    return ratNorm(n, d);
}

static std::pair<int, int> ratAdd(std::pair<int, int> a, std::pair<int, int> b) {
    const long long n = (long long)a.first * b.second + (long long)b.first * a.second;
    const long long d = (long long)a.second * b.second;
    return ratNorm(n, d);
}

static std::pair<int, int> ratMulInt(std::pair<int, int> a, int k) {
    return ratNorm((long long)a.first * k, a.second);
}

static std::pair<int, int> ratDivInt(std::pair<int, int> a, int n) {
    if (n == 0)
        return {0, 1};
    return ratNorm(a.first, (long long)a.second * n);
}

// Line k: lower + k * (upper - lower) / nSteps; per-slot rationals; trim trailing zero exponents.
static std::vector<std::pair<int, int>> edoVectorForK(int k, int nSteps,
                                                     const std::vector<std::pair<int, int>>& lowerIn,
                                                     const std::vector<std::pair<int, int>>& upperIn) {
    if (nSteps <= 0)
        return lowerIn;
    if (k == 0)
        return lowerIn;
    std::vector<std::pair<int, int>> lower(lowerIn);
    std::vector<std::pair<int, int>> upper(upperIn);
    const size_t N = std::max(lower.size(), upper.size());
    lower.resize(N, {0, 1});
    upper.resize(N, {0, 1});
    std::vector<std::pair<int, int>> out;
    out.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        const std::pair<int, int> diff = ratSub(upper[i], lower[i]);
        const std::pair<int, int> step = ratDivInt(diff, nSteps);
        const std::pair<int, int> inc = ratMulInt(step, k);
        out.push_back(ratAdd(lower[i], inc));
    }
    while (!out.empty() && out.back().first == 0)
        out.pop_back();
    return out;
}

static std::vector<std::pair<int, int>> alignedRatAddVectors(const std::vector<std::pair<int, int>>& a,
                                                           const std::vector<std::pair<int, int>>& b) {
    const size_t N = std::max(a.size(), b.size());
    std::vector<std::pair<int, int>> aa(a);
    std::vector<std::pair<int, int>> bb(b);
    aa.resize(N, {0, 1});
    bb.resize(N, {0, 1});
    std::vector<std::pair<int, int>> out;
    out.reserve(N);
    for (size_t i = 0; i < N; ++i)
        out.push_back(ratAdd(aa[i], bb[i]));
    while (!out.empty() && out.back().first == 0)
        out.pop_back();
    return out;
}

static std::vector<std::pair<int, int>> alignedRatSubVectors(const std::vector<std::pair<int, int>>& a,
                                                           const std::vector<std::pair<int, int>>& b) {
    const size_t N = std::max(a.size(), b.size());
    std::vector<std::pair<int, int>> aa(a);
    std::vector<std::pair<int, int>> bb(b);
    aa.resize(N, {0, 1});
    bb.resize(N, {0, 1});
    std::vector<std::pair<int, int>> out;
    out.reserve(N);
    for (size_t i = 0; i < N; ++i)
        out.push_back(ratSub(aa[i], bb[i]));
    while (!out.empty() && out.back().first == 0)
        out.pop_back();
    return out;
}

static bool isDefaultHarmonicReference(int anchorHarmonic, float anchorMidi) {
    return std::max(1, anchorHarmonic) == 1 && std::fabs(anchorMidi - 69.0f) < 1e-4f;
}

static std::vector<std::pair<int, int>> harmonicAnchorBaseFromNoteAtHarmonic(
    const std::vector<std::pair<int, int>>& notePairsRaw,
    int anchorHarmonic) {
    return alignedRatSubVectors(notePairsRaw, densePrimeExponentPairsForHarmonic(anchorHarmonic));
}

static std::vector<std::pair<int, int>> harmonicLineVectorFromAnchorBase(const std::vector<std::pair<int, int>>& anchorBase,
                                                                        int lineHarmonicH) {
    return alignedRatAddVectors(anchorBase, densePrimeExponentPairsForHarmonic(lineHarmonicH));
}

// Dense exponents for primes 2,3,5,… ; empty vector → "0"; zero exponent prints as "0" not "(0/den)".
static std::string formatPrimePowerVector(const std::vector<std::pair<int, int>>& v) {
    if (v.empty())
        return "0";
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        const int num = v[i].first;
        const int den = std::max(1, v[i].second);
        if (!s.empty())
            s += ", ";
        if (num == 0)
            s += '0';
        else {
            s += std::to_string(num);
            s += '/';
            s += std::to_string(den);
        }
    }
    return s;
}

/** True after Define EDO interval (or undo restoring that state); enables mode-bar switch to EDO.
 *  Here "nonzero" for a boundary = vector non-empty; the rational zero vector is stored as empty. */
static bool regionHasDragDefinedEdoLattice(const Region* r) {
    if (!r || r->tuningEdoSpanDivisions <= 0)
        return false;
    return !r->tuningEdoLowerVector.empty() || !r->tuningEdoUpperVector.empty();
}

} // namespace

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

float PianoRoll::harmonicToMidi(int harmonic) const {
    const int h = std::max(1, harmonic);
    const int anchorH = std::max(1, harmonicAnchorNumber);
    return harmonicAnchorMidi + 12.0f * std::log2(static_cast<float>(h) / static_cast<float>(anchorH));
}

void PianoRoll::applyHarmonicAnchor(float midi, int harmonic) {
    tuningMode = TuningMode::Harmonic;
    harmonicAnchorMidi = midi;
    harmonicAnchorNumber = std::max(1, harmonic);
    if (region) {
        region->tuningHarmonicAnchorVector.clear();
        region->tuningEdoSpanDivisions = 0;
        region->tuningEdoSpanLoMidi = 0.0f;
        region->tuningEdoSpanHiMidi = 0.0f;
        region->tuningSpanLoHarm = 0;
        region->tuningSpanHiHarm = 0;
        region->tuningSpanLoEdoK = INT_MAX;
        region->tuningSpanHiEdoK = INT_MAX;
    }
    syncTuningToRegion();
    updateLines();
}

void PianoRoll::defineEdoFromInterval(float a, float b, int steps) {
    const int n = std::max(1, steps);
    const float lo = std::min(a, b);
    const float hi = std::max(a, b);
    const float diff = std::max(1e-6f, hi - lo);
    tuningMode = TuningMode::EDO;
    edoAnchorMidi = lo;
    edoStep = diff / static_cast<float>(n);
    if (region) {
        region->tuningHarmonicAnchorVector.clear();
        region->tuningEdoSpanDivisions = n;
        region->tuningEdoSpanLoMidi = lo;
        region->tuningEdoSpanHiMidi = hi;
        region->tuningEdoSubdivisionSteps = n;
        region->tuningSpanLoHarm = 0;
        region->tuningSpanHiHarm = 0;
        region->tuningSpanLoEdoK = INT_MAX;
        region->tuningSpanHiEdoK = INT_MAX;
    }
    syncTuningToRegion();
    updateLines();
}

static RegionTuningSnapshot captureRegionTuning(const Region* region) {
    RegionTuningSnapshot s;
    if (!region) return s;
    s.mode = region->tuningMode;
    s.harmonicAnchorMidi = region->tuningAnchorMidi;
    s.harmonicAnchorNumber = region->tuningAnchorHarmonic;
    s.edoAnchorMidi = region->tuningEdoAnchorMidi;
    s.edoStep = region->tuningEdoStep;
    s.edoSpanDivisions = region->tuningEdoSpanDivisions;
    s.edoSpanLoMidi = region->tuningEdoSpanLoMidi;
    s.edoSpanHiMidi = region->tuningEdoSpanHiMidi;
    s.spanLoHarm = region->tuningSpanLoHarm;
    s.spanHiHarm = region->tuningSpanHiHarm;
    s.spanLoEdoK = region->tuningSpanLoEdoK;
    s.spanHiEdoK = region->tuningSpanHiEdoK;
    s.edoStepSemiNum = region->tuningEdoStepSemiNum;
    s.edoStepSemiDen = region->tuningEdoStepSemiDen;
    s.edoSubdivisionSteps = region->tuningEdoSubdivisionSteps;
    s.edoLowerVector = region->tuningEdoLowerVector;
    s.edoUpperVector = region->tuningEdoUpperVector;
    s.harmonicAnchorVector = region->tuningHarmonicAnchorVector;
    return s;
}

static void applyRegionTuning(Region* region, const RegionTuningSnapshot& s) {
    if (!region) return;
    region->tuningMode = s.mode;
    region->tuningAnchorMidi = s.harmonicAnchorMidi;
    region->tuningAnchorHarmonic = s.harmonicAnchorNumber;
    region->tuningEdoAnchorMidi = s.edoAnchorMidi;
    region->tuningEdoStep = s.edoStep;
    region->tuningEdoSpanDivisions = s.edoSpanDivisions;
    region->tuningEdoSpanLoMidi = s.edoSpanLoMidi;
    region->tuningEdoSpanHiMidi = s.edoSpanHiMidi;
    region->tuningSpanLoHarm = s.spanLoHarm;
    region->tuningSpanHiHarm = s.spanHiHarm;
    region->tuningSpanLoEdoK = s.spanLoEdoK;
    region->tuningSpanHiEdoK = s.spanHiEdoK;
    region->tuningEdoStepSemiNum = std::max(1, s.edoStepSemiNum);
    region->tuningEdoStepSemiDen = std::max(1, s.edoStepSemiDen);
    region->tuningEdoSubdivisionSteps = std::max(1, s.edoSubdivisionSteps);
    region->tuningEdoLowerVector = s.edoLowerVector;
    region->tuningEdoUpperVector = s.edoUpperVector;
    region->tuningHarmonicAnchorVector = s.harmonicAnchorVector;
}

static json regionTuningSnapshotToUndoJson(const RegionTuningSnapshot& s) {
    json j;
    j["tuningMode"] = s.mode;
    j["tuningAnchorMidi"] = s.harmonicAnchorMidi;
    j["tuningAnchorHarmonic"] = s.harmonicAnchorNumber;
    j["tuningEdoAnchorMidi"] = s.edoAnchorMidi;
    j["tuningEdoStep"] = s.edoStep;
    j["tuningEdoSpanDivisions"] = s.edoSpanDivisions;
    j["tuningEdoSpanLoMidi"] = s.edoSpanLoMidi;
    j["tuningEdoSpanHiMidi"] = s.edoSpanHiMidi;
    j["tuningSpanLoHarm"] = s.spanLoHarm;
    j["tuningSpanHiHarm"] = s.spanHiHarm;
    j["tuningSpanLoEdoK"] = s.spanLoEdoK;
    j["tuningSpanHiEdoK"] = s.spanHiEdoK;
    j["tuningEdoStepSemiNum"] = s.edoStepSemiNum;
    j["tuningEdoStepSemiDen"] = s.edoStepSemiDen;
    j["tuningEdoSubdivisionSteps"] = s.edoSubdivisionSteps;
    j["tuningEdoLowerVector"] = json::array();
    for (const auto& pr : s.edoLowerVector)
        j["tuningEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["tuningEdoUpperVector"] = json::array();
    for (const auto& pr : s.edoUpperVector)
        j["tuningEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    j["tuningHarmonicAnchorVector"] = json::array();
    for (const auto& pr : s.harmonicAnchorVector)
        j["tuningHarmonicAnchorVector"].push_back(json::array({pr.first, pr.second}));
    return j;
}

static json noteTuningSnapshotToUndoJson(const NoteTuningSnapshot& s) {
    json j;
    j["harmonicNumber"] = s.harmonicNumber;
    j["tuningMode"] = s.tuningMode;
    j["tuningAnchorMidi"] = s.tuningAnchorMidi;
    j["tuningAnchorHarmonic"] = s.tuningAnchorHarmonic;
    j["tuningEdoAnchorMidi"] = s.tuningEdoAnchorMidi;
    j["tuningEdoStep"] = s.tuningEdoStep;
    return j;
}

static NoteTuningSnapshot captureNoteTuning(const std::shared_ptr<Note>& n) {
    NoteTuningSnapshot s;
    if (!n) return s;
    s.harmonicNumber = n->harmonicNumber;
    s.tuningMode = n->tuningMode;
    s.tuningAnchorMidi = n->tuningAnchorMidi;
    s.tuningAnchorHarmonic = n->tuningAnchorHarmonic;
    s.tuningEdoAnchorMidi = n->tuningEdoAnchorMidi;
    s.tuningEdoStep = n->tuningEdoStep;
    return s;
}

static void applyNoteTuningSnapshot(const std::shared_ptr<Note>& n, const NoteTuningSnapshot& s) {
    if (!n) return;
    n->harmonicNumber = s.harmonicNumber;
    n->tuningMode = s.tuningMode;
    n->tuningAnchorMidi = s.tuningAnchorMidi;
    n->tuningAnchorHarmonic = s.tuningAnchorHarmonic;
    n->tuningEdoAnchorMidi = s.tuningEdoAnchorMidi;
    n->tuningEdoStep = s.tuningEdoStep;
}

void PianoRoll::applyNoteTuning(const std::shared_ptr<Note>& note) {
    if (!note) return;
    tuningMode = (note->tuningMode == 1) ? TuningMode::EDO : TuningMode::Harmonic;
    harmonicAnchorMidi = note->tuningAnchorMidi;
    harmonicAnchorNumber = std::max(1, note->tuningAnchorHarmonic);
    edoAnchorMidi = note->tuningEdoAnchorMidi;
    edoStep = std::max(1e-5f, note->tuningEdoStep);
    syncTuningToRegion();
    updateLines();
}

void PianoRoll::stampNoteTuning(const std::shared_ptr<Note>& note) {
    if (!note) return;
    note->tuningMode = (tuningMode == TuningMode::EDO) ? 1 : 0;
    note->tuningAnchorMidi = harmonicAnchorMidi;
    note->tuningAnchorHarmonic = harmonicAnchorNumber;
    note->tuningEdoAnchorMidi = edoAnchorMidi;
    note->tuningEdoStep = edoStep;
    note->syncNumFromPitchIntegerPairs();
}

void PianoRoll::syncTuningToRegion() {
    if (!region) return;
    region->tuningMode = static_cast<int>(tuningMode);
    region->tuningAnchorMidi = harmonicAnchorMidi;
    region->tuningAnchorHarmonic = harmonicAnchorNumber;
    region->tuningEdoAnchorMidi = edoAnchorMidi;
    region->tuningEdoStep = edoStep;
}

void PianoRoll::loadTuningFromRegion() {
    if (!region) return;
    tuningMode = (region->tuningMode == 1) ? TuningMode::EDO : TuningMode::Harmonic;
    harmonicAnchorMidi = region->tuningAnchorMidi;
    harmonicAnchorNumber = std::max(1, region->tuningAnchorHarmonic);
    edoAnchorMidi = region->tuningEdoAnchorMidi;
    edoStep = std::max(1e-5f, region->tuningEdoStep);
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
        const bool defaultHarmonicRef = isDefaultHarmonicReference(harmonicAnchorNumber, harmonicAnchorMidi);
        static const std::vector<std::pair<int, int>> kEmptyHarmonicAnchor;
        for (int h = 1; h <= 512; ++h) {
            const float midi = harmonicToMidi(h);
            if (midi < -24.0f || midi > 152.0f) continue;
            pitchLines.emplace_back(midi);
            if (defaultHarmonicRef) {
                if (h == 1)
                    pitchLines.back().integerPairs.clear();
                else
                    pitchLines.back().integerPairs = densePrimeExponentPairsForHarmonic(h);
            } else {
                const std::vector<std::pair<int, int>>& anchorBase =
                    region ? region->tuningHarmonicAnchorVector : kEmptyHarmonicAnchor;
                pitchLines.back().integerPairs = harmonicLineVectorFromAnchorBase(anchorBase, h);
            }
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
    if (n->harmonicNumber > 0) {
        for (size_t i = 0; i < lineStructural.size(); ++i) {
            if (lineStructural[i] == n->harmonicNumber)
                return std::max(1, n->harmonicNumber);
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

PianoRoll::PianoRoll(Region* region_, Window* parent)
    : EmbeddedWindow(),
      region(region_),
      GridView(nullptr, 40, parent, region_->project)
{
    leftMargin = 80.0f;

    updateLines();

    scrollY = 800;

    divHeight = 200; //octaveheight

    minHeight = 12.0f / 128;

    bottomMargin = 20;

    UpdateGrid();

    Scroll();

    float x = -1000; //for now only this many measures
    times.clear();
    while(x < 1000) {
        times.push_back(x);
        x += 1.0f/notesPerBar;
    }

    createGridRect();

    // Sync EmbeddedWindow dimensions with GridView.
    EmbeddedWindow::w = width;
    EmbeddedWindow::h = height;
    title = "Piano Roll";
}

PianoRoll::~PianoRoll() {
    for(int i = 0; i<4; i++) {
        SDL_DestroyTexture(layers[i]);
    }
    for (auto* t : lineLabelTextures) SDL_DestroyTexture(t);
}

static void pianoRollSyncCoords(PianoRoll* pr) {
    // Sync dstRect/gridRect to current embedded window position and size.
    float newW = pr->EmbeddedWindow::w - EmbeddedWindow::kBorderW * 2.f;
    float newH = pr->EmbeddedWindow::h - EmbeddedWindow::kTitleBarH - EmbeddedWindow::kBorderW;
    if (newW < 100.f) newW = 100.f;
    if (newH < 80.f) newH = 80.f;

    if (pr->width != newW || pr->height != newH) {
        pr->needsInit_ = true;
    }
    pr->width = newW;
    pr->height = newH;
    pr->dstRect->w = newW;
    pr->dstRect->h = newH;

    pr->dstRect->x = pr->EmbeddedWindow::x + EmbeddedWindow::kBorderW;
    pr->dstRect->y = pr->EmbeddedWindow::y + EmbeddedWindow::kTitleBarH;
    pr->gridRect.x = pr->leftMargin;
    pr->gridRect.y = pr->topMargin;
    pr->gridRect.w = newW - pr->leftMargin;
    pr->gridRect.h = newH - pr->topMargin - pr->bottomMargin;

    // Always start from global mouse pos, then subtract content origin.
    float gx, gy;
    SDL_GetMouseState(&gx, &gy);
    pr->mouseX = gx - pr->dstRect->x;
    pr->mouseY = gy - pr->dstRect->y;
    pr->transport->moveMouse(pr->mouseX, pr->mouseY);
}

bool PianoRoll::handleContentInput(SDL_Event& e) {
    pianoRollSyncCoords(this);
    return GridView::handleInput(e);
}

bool PianoRoll::handleInput(SDL_Event& e) {
    bool consumed = EmbeddedWindow::handleInput(e);
    pianoRollSyncCoords(this);
    return GridView::handleInput(e) || consumed;
}

void PianoRoll::renderContent(SDL_Renderer* r) {
    pianoRollSyncCoords(this);
    customTick(r);
}



void PianoRoll::UpdateGrid() {
    if(notesPerOctave <= 0) {
        notesPerOctave = 1;
    } else if(notesPerOctave > 128) {
        notesPerOctave = 128;
    }
    cellHeight = divHeight/notesPerOctave;
    cellHeight12 = divHeight/12.0;

    double a440 = cellHeight12*59;
    yMin = cellHeight12*59 - std::floor(cellHeight12*59/cellHeight)*cellHeight;
    yMax = cellHeight12*69 - std::floor(cellHeight12*69/cellHeight)*cellHeight;

    Scroll();
}

double PianoRoll::getNoteName(double y) {
    return 129-(y + scrollY)/cellHeight12;
}

float PianoRoll::getY(float noteMidiNum) {
    return -cellHeight12*((noteMidiNum-129)+(scrollY/cellHeight12)) - lineWidth;
}

void PianoRoll::renderPianoRollGridTexture(SDL_Renderer* renderer) {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    setRenderColor(renderer, colors.grid);

    for (auto line : times) {
        float val = getX(line);
        SDL_RenderLine(renderer, val, 0, val, height);
    }

    for (const auto& pl : pitchLines) {
        float val = getY(pl.midi);
        SDL_RenderLine(renderer, 0, val, width, val);
    }
    SDL_SetRenderTarget(renderer, target);
}

float PianoRoll::getHoveredLine() {
    float closestDiff = FLT_MAX;
    float closestLine = -1.0f;

    for (const auto& pl : pitchLines) {
        float y = getY(pl.midi);
        float diff = std::abs(mouseY - y);
        if (diff < closestDiff) {
            closestDiff = diff;
            closestLine = pl.midi;
        }
    }

    return closestLine;
}


void PianoRoll::RenderDestinations(SDL_Renderer* renderer) {

    if (fonts.mainFont) {
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: mainFont is NULL in PianoRoll::RenderDestinations!\n");
        return;
    }

    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, PianoTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);
    SDL_Color textColor = {0, 0, 0, 255};


    SDL_FRect backgroundRect = {0, topMargin, leftMargin, height - topMargin - bottomMargin};

    setRenderColor(renderer, colors.keyWhite);
    SDL_RenderFillRect(renderer, &backgroundRect);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);
    SDL_RenderLine(renderer, leftMargin+1,topMargin,leftMargin+1,height - topMargin - bottomMargin);

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);

    if (lineLabelTextures.size() != pitchLines.size()) {
        for (auto* t : lineLabelTextures) SDL_DestroyTexture(t);
        lineLabelTextures.clear();
        lineLabelTextures.reserve(pitchLines.size());
        for (size_t i = 0; i < pitchLines.size(); ++i) {
            const std::string& label = (i < lineLabels.size()) ? lineLabels[i] : std::to_string(pitchLines[i].midi);
            SDL_Surface* surf = TTF_RenderText_Solid(fonts.mainFont, label.c_str(), label.size(), textColor);
            lineLabelTextures.push_back(SDL_CreateTextureFromSurface(renderer, surf));
            SDL_DestroySurface(surf);
        }
    }

    for (size_t i = 0; i < pitchLines.size(); ++i) {
        float y = getY(pitchLines[i].midi);
        SDL_Texture* tex = lineLabelTextures[i];
        float tw, th;
        SDL_GetTextureSize(tex, &tw, &th);
        SDL_FRect textRect = {0, y - th/2, tw, th};
        SDL_RenderLine(renderer, tw, y, leftMargin, y);
        SDL_RenderTexture(renderer, tex, NULL, &textRect);
    }
    
    SDL_SetRenderTarget(renderer, target);

}

void PianoRoll::Scroll() {


    numCellsRight = (scrollX)/dW;
    numCellsDown = (scrollY-yMin + topMargin)/cellHeight;
    numCellsDown12 = scrollY/cellHeight12;
    if((scrollY + topMargin -yMin - cellHeight12) <= 0) {
        scrollY = yMin + cellHeight12 - topMargin;
    } else {
        if(scrollY+height+yMin+yMax >= 129*cellHeight12) {
            scrollY = 129*cellHeight12 - height - yMin -yMax;
        }
    }
                numCellsDown = (scrollY-yMin)/cellHeight;


    yOffset = (std::ceil(numCellsDown) * cellHeight) - scrollY;
    yOffset12 = (std::ceil(numCellsDown12) * cellHeight12) - scrollY;

    xOffset = (std::ceil(numCellsRight) * dW) - scrollX;

        refreshGrid = true;
        getStretchingNote();
        getExistingNote();
        refreshHoveredPitchLineIndex();
        refreshPitchFactorsHoverTiming();
}

bool PianoRoll::customTick(SDL_Renderer* renderer) {
    if (!backgroundTexture || needsInit_) {
        needsInit_ = false;
        initWindow(renderer);
    }

    if(refreshGrid) {
        refreshGrid = false;
        renderPianoRollGridTexture(renderer);
        RenderDestinations(renderer);
    }

    RenderNotes(renderer);

    SDL_RenderTexture(renderer, backgroundTexture, nullptr, dstRect);

    const bool showIntervalPreview = selectingInterval || intervalEdoDefineDialogOpen;
    const float visIntervalStartLine =
        selectingInterval ? intervalStartLine : intervalDialogFrozenStartLine;
    const float visIntervalEndLine = selectingInterval ? intervalEndLine : intervalDialogFrozenEndLine;

    if (showIntervalPreview) {
        const float yStart = getY(visIntervalStartLine);
        const float yEnd = getY(visIntervalEndLine);
        const float yTop = std::min(yStart, yEnd);
        const float yBot = std::max(yStart, yEnd);
        SDL_FRect band{dstRect->x + leftMargin, dstRect->y + yTop, width - leftMargin, std::max(1.0f, yBot - yTop)};
        SDL_SetRenderDrawColor(renderer, 45, 110, 210, 32);
        SDL_RenderFillRect(renderer, &band);
        SDL_SetRenderDrawColor(renderer, 70, 150, 235, 78);
        SDL_RenderRect(renderer, &band);
    }

    SDL_RenderTexture(renderer, gridTexture, nullptr, dstRect);
    SDL_RenderTexture(renderer, NotesTexture, nullptr, dstRect);

    if(project->processing) {
       for(auto pos : region->positions) {
           playHead->render(renderer, dW, scrollX + (float)pos->start * dW);
        }
    }

    SDL_RenderTexture(renderer, PianoTexture, nullptr, dstRect);

    if (showIntervalPreview) {
        const float yEnd = getY(visIntervalEndLine);
        SDL_SetRenderDrawColor(renderer, 65, 190, 240, 128);
        SDL_RenderLine(renderer, dstRect->x + mouseX, dstRect->y + yEnd, dstRect->x + leftMargin, dstRect->y + yEnd);
    }

    transport->render(renderer);

    SDL_FRect bottomRect{
        dstRect->x,
        dstRect->y + height - bottomMargin,
        width,
        bottomMargin
    };
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &bottomRect);

    modeButtonRect = SDL_FRect{
        dstRect->x + 8.0f,
        dstRect->y + height - bottomMargin + 3.0f,
        220.0f,
        std::max(12.0f, bottomMargin - 6.0f)
    };
    const bool edoModeBarLocked =
        region && tuningMode == TuningMode::Harmonic && !regionHasDragDefinedEdoLattice(region);
    SDL_SetRenderDrawColor(renderer, edoModeBarLocked ? 42 : 50, edoModeBarLocked ? 42 : 50, edoModeBarLocked ? 44 : 50, 255);
    SDL_RenderFillRect(renderer, &modeButtonRect);
    SDL_SetRenderDrawColor(renderer, edoModeBarLocked ? 92 : 130, edoModeBarLocked ? 92 : 130, edoModeBarLocked ? 96 : 130, 255);
    SDL_RenderRect(renderer, &modeButtonRect);
    if (fonts.mainFont) {
        const char* modeText = (tuningMode == TuningMode::Harmonic)
            ? (edoModeBarLocked ? "Mode: Harmonic (EDO: drag interval)" : "Mode: Harmonic")
            : "Mode: EDO";
        const SDL_Color modeColor =
            edoModeBarLocked ? SDL_Color{155, 160, 170, 255} : SDL_Color{230, 230, 230, 255};
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, modeText, 0, modeColor);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                const float scale = std::min(1.0f, (modeButtonRect.w - 10.0f) / static_cast<float>(surf->w));
                SDL_FRect tr{
                    modeButtonRect.x + (modeButtonRect.w - surf->w * scale) * 0.5f,
                    modeButtonRect.y + (modeButtonRect.h - surf->h * scale) * 0.5f,
                    surf->w * scale,
                    surf->h * scale
                };
                SDL_RenderTexture(renderer, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }

    renderPitchFactorsHoverTooltip();

    return true;
}

void PianoRoll::initWindow(SDL_Renderer* renderer) {

    dstRect->w = width;
    dstRect->h = height;

    gridRect = {
        dstRect->x+leftMargin,
        dstRect->y+topMargin,
        dstRect->w-leftMargin,
        dstRect->h-topMargin - bottomMargin
    };

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(gridTexture);
    SDL_DestroyTexture(PianoTexture);
    SDL_DestroyTexture(KeyTexture);
    SDL_DestroyTexture(NotesTexture);
    for (auto* t : lineLabelTextures) SDL_DestroyTexture(t);
    lineLabelTextures.clear();

    backgroundTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    PianoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    KeyTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    NotesTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    layers[0] = backgroundTexture;
    layers[1] = gridTexture;
    layers[2] = NotesTexture;
    layers[3] = PianoTexture;

    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, backgroundTexture);
    setRenderColor(renderer, colors.background);

    SDL_RenderClear(renderer); // Clear backgroundTexture with the background color

    SDL_SetRenderTarget(renderer, target);

    if(height > (128*cellHeight12 - yMax - yMin)) {
        divHeight = 12*height/128;
        UpdateGrid();
        
    }
    
    Scroll();
    renderPianoRollGridTexture(renderer);

    RenderDestinations(renderer);

    RenderNotes(renderer);

}

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
                        intervalDragStartVertexPairs = intervalStartNote->pitchIntegerPairs;
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
                if (mouseX > leftMargin) {
                    getExistingNote();
                    getStretchingNote();
                    if (stretchingNote != nullptr) {
                        stretchingNoteUndoBefore = stretchingNote->toJSON();
                        stretchingNoteHasUndoSnapshot = true;
                        stretchingNoteDragDirty = false;
                        last_lmb_x = mouseX;
                        handleMouse();
                        refreshGrid = true;
                        return;
                    }
                    if (hoveredElement == nullptr) {
                        createElement();
                    } else {
                        auto n = std::dynamic_pointer_cast<Note>(hoveredElement);
                        if (isShiftPressed) {
                            if (!n) return;
                            auto before = captureRegionTuning(region);
                            auto after = before;
                            after.mode = n->tuningMode;
                            after.harmonicAnchorMidi = n->tuningAnchorMidi;
                            after.harmonicAnchorNumber = n->tuningAnchorHarmonic;
                            after.edoAnchorMidi = n->tuningEdoAnchorMidi;
                            after.edoStep = n->tuningEdoStep;
                            if (after.mode == 0) {
                                if (isDefaultHarmonicReference(after.harmonicAnchorNumber, after.harmonicAnchorMidi))
                                    after.harmonicAnchorVector.clear();
                                else
                                    after.harmonicAnchorVector = harmonicAnchorBaseFromNoteAtHarmonic(
                                        n->pitchIntegerPairs, after.harmonicAnchorNumber);
                            } else {
                                after.harmonicAnchorVector.clear();
                            }
                            project->um->newAction(new PianoRollRegionTuningUndoAction(project, region->parentNode->nm->managerPath,
                                region->parentNode->id, region->id, regionTuningSnapshotToUndoJson(before),
                                regionTuningSnapshotToUndoJson(after), "Recall Note Tuning View"));
                            return;
                        }
                        movingNote = n;
                        movingNoteUndoBefore = n->toJSON();
                        movingNoteHasUndoSnapshot = true;
                        movingNoteDragDirty = false;
                        movingNotePitchPreviewLineMidi.reset();
                        last_lmb_x = mouseX;
                    }
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                if(mouseX > leftMargin && stretchingNote == nullptr) {
                    if(isShiftPressed && hoveredElement != nullptr) {
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
                                afterRegion.harmonicAnchorMidi = note->num;
                                afterRegion.harmonicAnchorNumber = h;
                                if (isDefaultHarmonicReference(h, note->num))
                                    afterRegion.harmonicAnchorVector.clear();
                                else
                                    afterRegion.harmonicAnchorVector =
                                        harmonicAnchorBaseFromNoteAtHarmonic(note->pitchIntegerPairs, h);
                                auto afterNote = beforeNote;
                                afterNote.harmonicNumber = h;
                                afterNote.tuningMode = 0;
                                afterNote.tuningAnchorMidi = note->num;
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
                if (stretchingNoteHasUndoSnapshot && stretchingNote) {
                    json afterStretch = stretchingNote->toJSON();
                    if (stretchingNoteDragDirty || afterStretch != stretchingNoteUndoBefore) {
                        project->um->newAction(new MoveNoteAction(project, region->parentNode->nm->managerPath,
                            region->parentNode->id, region->id, stretchingNote->id, std::move(stretchingNoteUndoBefore),
                            std::move(afterStretch), "Resize Note"));
                    }
                    stretchingNoteHasUndoSnapshot = false;
                    stretchingNoteDragDirty = false;
                }
                if (movingNoteHasUndoSnapshot && movingNote) {
                    if (movingNotePitchPreviewLineMidi) {
                        commitNotePitchSnap(movingNote, *movingNotePitchPreviewLineMidi);
                        movingNotePitchPreviewLineMidi.reset();
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
                    const bool capStartedHarm = intervalSelectStartedHarmonic;
                    const int capHarmA = intervalDragHarmA;
                    const int capHarmB = intervalDragHarmB;
                    const int capEdoKA = intervalDragEdoKA;
                    const int capEdoKB = intervalDragEdoKB;
                    bool intervalFromTwoNotes = false;
                    std::vector<std::pair<int, int>> intervalLoNotePairs;
                    std::vector<std::pair<int, int>> intervalHiNotePairs;
                    if (intervalStartNote && endNote && intervalDragMoved &&
                        intervalStartNote->id != endNote->id) {
                        intervalFromTwoNotes = true;
                        if (intervalStartNote->num <= endNote->num) {
                            intervalLoNotePairs = intervalStartNote->pitchIntegerPairs;
                            intervalHiNotePairs = endNote->pitchIntegerPairs;
                        } else {
                            intervalLoNotePairs = endNote->pitchIntegerPairs;
                            intervalHiNotePairs = intervalStartNote->pitchIntegerPairs;
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
                        [this, a, b, capStartedHarm, capHarmA, capHarmB, capEdoKA, capEdoKB, intervalFromTwoNotes,
                            intervalLoNotePairs, intervalHiNotePairs, capIntervalStartNote, capIntervalEndNote,
                            capDragStartVertexPairs, capDragEndVertexPairs](std::string text) {
                        try {
                            const int steps = std::max(1, std::stoi(text));
                            const float loF = std::min(a, b);
                            const float hiF = std::max(a, b);
                            // Snapshots track rational endpoints while dragging; b can be off-grid note MIDI with
                            // hoveredElement on the staff (endNote null) — never pitchIntegerPairsAtGridMidi(note->num).
                            const std::vector<std::pair<int, int>> vecAtA =
                                capIntervalStartNote ? capDragStartVertexPairs : pitchIntegerPairsAtGridMidi(a);
                            const std::vector<std::pair<int, int>> vecAtB =
                                !capDragEndVertexPairs.empty()
                                    ? capDragEndVertexPairs
                                    : (capIntervalEndNote ? capIntervalEndNote->pitchIntegerPairs
                                                          : pitchIntegerPairsAtGridMidi(b));
                            const std::vector<std::pair<int, int>> lowerLineVec =
                                intervalFromTwoNotes ? intervalLoNotePairs : (a <= b ? vecAtA : vecAtB);
                            const std::vector<std::pair<int, int>> upperLineVec =
                                intervalFromTwoNotes ? intervalHiNotePairs : (a <= b ? vecAtB : vecAtA);

                            auto before = captureRegionTuning(region);
                            auto after = before;

                            if (intervalFromTwoNotes) {
                                // Notes may sit off grid lines: use their rational vectors and exact MIDI span, not
                                // nearest-line pairs or structural harmonic/k span (which can look like an octave).
                                after.spanLoHarm = 0;
                                after.spanHiHarm = 0;
                                after.spanLoEdoK = INT_MAX;
                                after.spanHiEdoK = INT_MAX;
                                after.edoSpanLoMidi = loF;
                                after.edoSpanHiMidi = hiF;
                                after.edoAnchorMidi = loF;
                                const float diff = std::max(1e-6f, hiF - loF);
                                after.edoStep = diff / static_cast<float>(steps);
                                after.edoSpanDivisions = steps;
                                after.mode = 1;
                            } else if (capStartedHarm && capHarmA > 0 && capHarmB > 0) {
                                const int hLo = std::min(capHarmA, capHarmB);
                                const int hHi = std::max(capHarmA, capHarmB);
                                after.spanLoHarm = hLo;
                                after.spanHiHarm = hHi;
                                after.spanLoEdoK = INT_MAX;
                                after.spanHiEdoK = INT_MAX;
                                const int ah = std::max(1, after.harmonicAnchorNumber);
                                const auto harmToMidi = [ah](float anchorMidi, int h) {
                                    return anchorMidi + 12.0f * std::log2f(static_cast<float>(std::max(1, h)) /
                                                                           static_cast<float>(ah));
                                };
                                const float loM = harmToMidi(after.harmonicAnchorMidi, hLo);
                                const float hiM = harmToMidi(after.harmonicAnchorMidi, hHi);
                                after.edoSpanLoMidi = std::min(loM, hiM);
                                after.edoSpanHiMidi = std::max(loM, hiM);
                                after.edoAnchorMidi = after.edoSpanLoMidi;
                                const float diff = std::max(1e-6f, after.edoSpanHiMidi - after.edoSpanLoMidi);
                                after.edoStep = diff / static_cast<float>(steps);
                                after.edoSpanDivisions = steps;
                                after.mode = 1;
                            } else if (!capStartedHarm && capEdoKA != INT_MAX && capEdoKB != INT_MAX) {
                                // Span must match the drag (intervalStartLine / intervalEndLine → a, b), e.g. exact
                                // note->num. Do not rebuild from lattice k (structuralEdoK*); that snaps to lines.
                                after.spanLoHarm = 0;
                                after.spanHiHarm = 0;
                                after.edoSpanLoMidi = loF;
                                after.edoSpanHiMidi = hiF;
                                after.edoAnchorMidi = after.edoSpanLoMidi;
                                const float diff = std::max(1e-6f, after.edoSpanHiMidi - after.edoSpanLoMidi);
                                after.edoStep = diff / static_cast<float>(steps);
                                after.edoSpanDivisions = steps;
                                after.mode = 1;
                                after.spanLoEdoK = 0;
                                after.spanHiEdoK = steps;
                            } else {
                                const float diff = std::max(1e-6f, hiF - loF);
                                after.mode = 1;
                                after.edoAnchorMidi = loF;
                                after.edoStep = diff / static_cast<float>(steps);
                                after.edoSpanDivisions = steps;
                                after.edoSpanLoMidi = loF;
                                after.edoSpanHiMidi = hiF;
                                after.spanLoHarm = 0;
                                after.spanHiHarm = 0;
                                after.spanLoEdoK = INT_MAX;
                                after.spanHiEdoK = INT_MAX;
                            }
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

            if (selectingInterval) {
                auto endNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                if (endNote) {
                    intervalEndLine = endNote->num;
                    intervalDragEndVertexPairs = endNote->pitchIntegerPairs;
                    if (!intervalStartNote || endNote->id != intervalStartNote->id) {
                        intervalDragMoved = true;
                    }
                } else {
                    // Always follow the hovered lattice line. The old dNearest<dStart rule kept intervalEndLine
                    // pinned to intervalStartLine while the cursor was still nearer in Y to the start note, so loF
                    // stayed at the note MIDI and pitchIntegerPairsAtGridMidi(loF) used the wrong lattice row.
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

            
            if(stretchingNote != nullptr) {
                if(!lmb) {
                    isStretchingNote = false;
                } else {
                    refreshGrid = true;
                    float dX = mouseX - last_lmb_x;
                    if(dX >= dW/notesPerBar) {
                        stretchElement(1);
                        last_lmb_x += dW/notesPerBar;
                    } else if(dX <= -dW/notesPerBar) {
                        stretchElement(-1);
                        last_lmb_x -= dW/notesPerBar;
                    }
                }

            } else if(lmb && movingNote != nullptr) {
                refreshGrid = true;

                float dX = mouseX - last_lmb_x;
                float step = dW / notesPerBar;
                int steps = dX / step;

                if(steps) {
                    moveNoteTime(movingNote, steps);
                    last_lmb_x += steps * step;
                }

                constexpr float pitchDragEpsilon = 1e-3f;
                const float snappedLine = getHoveredLine();
                if (std::fabs(snappedLine - movingNote->num) > pitchDragEpsilon)
                    movingNotePitchPreviewLineMidi = snappedLine;
                else
                    movingNotePitchPreviewLineMidi.reset();
                
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
    fract start = getHoveredTime();
    const float pitch = getHoveredLine();
    const size_t li = closestLineIndexForMidi(pitch);
    std::vector<std::pair<int, int>> pitchPairs;
    if (li != SIZE_MAX && li < pitchLines.size())
        pitchPairs = pitchLines[li].integerPairs;
    project->createNote(
        region->parentNode->id,
        start,
        lastLength,
        pitch,
        region->id,
        region->parentNode->nm->managerPath,
        std::move(pitchPairs)
    );
    if (!region->notes.empty()) {
        auto note = region->notes.back();
        refreshHoveredPitchLineIndex();
        const size_t li = hoveredPitchLineIndex;
        if (tuningMode == TuningMode::Harmonic) {
            if (li != SIZE_MAX && li < lineStructural.size())
                note->harmonicNumber = std::max(1, lineStructural[li]);
            else
                note->harmonicNumber = 1;
        } else {
            note->harmonicNumber = 0;
        }
        stampNoteTuning(note);
        // CreateNoteAction::doAction does not rerun stampNoteTuning on redo; persist post-stamp state on the action.
        if (project && project->um && project->um->current && project->um->current->type == CreateNote)
            static_cast<CreateNoteAction*>(project->um->current)->noteStampedSnapshot = note->toJSON();
    }
    refreshGrid = true;
}

void PianoRoll::RenderNotes(SDL_Renderer* renderer) {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, NotesTexture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

    //backgrounds first
    for(std::shared_ptr<Note> note : region->notes) {

        float noteX = getNotePosX(note) +1;
        float noteY = getY(noteMidiForRender(note));
        float noteEnd = getNoteEnd(note) -2;
        float noteTop = noteY + noteHeight;


        setRenderColor(renderer, colors.noteBackground);
        SDL_FRect noteBGRect = { noteX, noteY, noteEnd - noteX, noteTop-noteY};
        SDL_RenderFillRect(renderer, &noteBGRect);
    }

    for(std::shared_ptr<Note> note : region->notes) {
            float noteX = getNotePosX(note) +1;
            float noteY = getY(noteMidiForRender(note));
            float noteEnd = getNoteEnd(note) -2;

            //noteRadius = (noteTop - noteY)/2;

            setRenderColor(renderer, colors.note);
            SDL_FRect noteRect = { noteX, noteY - noteRadius, noteEnd - noteX, 2*noteRadius};
            SDL_RenderFillRect(renderer, &noteRect);

            setRenderColor(renderer, colors.noteBorder);

            SDL_RenderRect(renderer, &noteRect);

    }

    SDL_SetRenderTarget(renderer, target);

}

bool PianoRoll::getExistingNote() {
    hoveredElement = nullptr;
    if(mouseY < topMargin || mouseX < leftMargin) {
        return false;
    }
    int i = 0;
    for (std::shared_ptr<Note> note : region->notes) {
        
        // Get the required positions and size once per iteration
        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(noteMidiForRender(note));

        // Check if mouse is within note bounds
        if (mouseX >= notePosX && mouseX <= noteEnd &&
            mouseY <= noteY + noteRadius && mouseY >= (noteY - noteRadius)) {
            hoveredElement = note; // Found the hovered note
            lastHoveredLine = getHoveredLine();
            return true; // Exit early
        }
        i++;
    }
    return false;
}


float PianoRoll::getNotePosX(std::shared_ptr<Note> note) {
    return getX(note->start);
}

float PianoRoll::getNoteEnd(std::shared_ptr<Note> note) {
    return getX(note->end);
}

void PianoRoll::deleteElement() {
    if (hoveredElement == nullptr)
        return;
    auto note = std::dynamic_pointer_cast<Note>(hoveredElement);
    if (!note)
        return;
    const int nid = note->id;
    hoveredElement = nullptr;
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
        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(noteMidiForRender(note));

        if ((mouseY >= noteY - noteRadius && mouseY <= (noteY + noteRadius))) {
            if(mouseX >= notePosX - selectThresholdX/2 && mouseX <= notePosX + selectThresholdX/2) {
                stretchingNote = note;
                resizeDir = -1;
                isStretchingNote = true;
                return true;
            } else if (mouseX >= noteEnd - selectThresholdX/2 && mouseX <= noteEnd + selectThresholdX/2) {
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
    if(resizeDir == -1) {
        stretchingNote->start = stretchingNote->start + fract(amount,notesPerBar);
    } else if(resizeDir == 1) {
        stretchingNote->end = stretchingNote->end + fract(amount,notesPerBar);
    }

    if(stretchingNote->end < stretchingNote->start) {
        stretchingNote->end = stretchingNote->start;
    }
    lastLength = stretchingNote->end - stretchingNote->start;
    Scroll();
}


float PianoRoll::noteMidiForRender(const std::shared_ptr<Note>& note) const {
    if (movingNote && movingNote.get() == note.get() && movingNotePitchPreviewLineMidi)
        return *movingNotePitchPreviewLineMidi;
    return note->num;
}

void PianoRoll::moveNoteTime(std::shared_ptr<Note> note, int moveX) {
    if (moveX != 0)
        movingNoteDragDirty = true;
    const fract xm = fract(moveX, notesPerBar);
    note->start = note->start + xm;
    note->end = note->end + xm;
    Scroll();
}

void PianoRoll::commitNotePitchSnap(std::shared_ptr<Note> note, float targetLineMidi) {
    note->tuningAnchorMidi = harmonicAnchorMidi;
    note->tuningAnchorHarmonic = harmonicAnchorNumber;
    note->tuningEdoAnchorMidi = edoAnchorMidi;
    note->tuningEdoStep = edoStep;
    const size_t li = closestLineIndexForMidi(targetLineMidi);
    if (li != SIZE_MAX && li < pitchLines.size()) {
        note->pitchIntegerPairs = pitchLines[li].integerPairs;
        if (tuningMode == TuningMode::Harmonic && li < lineStructural.size())
            note->harmonicNumber = std::max(1, lineStructural[li]);
        movingNoteDragDirty = true;
    }
    stampNoteTuning(note);
    Scroll();
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

