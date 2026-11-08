#pragma once
// Shared internals for the PianoRoll implementation files (PianoRoll.cpp,
// PianoRollLattice.cpp, PianoRollInput.cpp): tuning snapshots and prime-power
// rational vector math. Not part of the public PianoRoll API.
#include <vector>
#include <string>
#include <sstream>
#include <cfloat>
#include <climits>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "Region.h"
#include "Note.h"

struct RegionTuningSnapshot {
    int mode = 0;
    int harmonicAnchorNumber = 1;
    int edoSubdivisionSteps = 12;
    std::vector<std::pair<int, int>> edoLowerVector;
    std::vector<std::pair<int, int>> edoUpperVector;
    std::vector<std::pair<int, int>> harmonicAnchorVector;
    int rhythmEdoSubdivisionSteps = 1;
    std::vector<std::pair<int, int>> rhythmEdoLowerVector;
    std::vector<std::pair<int, int>> rhythmEdoUpperVector;
    std::vector<RhythmLatticeSpan> transportLines;
    std::vector<RhythmLatticeSpan> gridSpans;
};

struct NoteTuningSnapshot {
    int tuningMode = 0;
    int tuningAnchorHarmonic = 1;
    int tuningEdoSubdivisionSteps = 12;
    std::vector<std::pair<int, int>> tuningEdoLowerVector;
    std::vector<std::pair<int, int>> tuningEdoUpperVector;
};


inline bool isPrimeInt(int p) {
    if (p < 2)
        return false;
    for (int d = 2; d * d <= p; ++d) {
        if (p % d == 0)
            return false;
    }
    return true;
}

inline std::vector<int> primesUpToInclusive(int maxP) {
    std::vector<int> out;
    for (int p = 2; p <= maxP; ++p) {
        if (isPrimeInt(p))
            out.push_back(p);
    }
    return out;
}

inline int largestPrimeFactor(int h) {
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
inline std::vector<std::pair<int, int>> densePrimeExponentPairsForHarmonic(int h) {
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

inline std::pair<int, int> ratNorm(long long num, long long den) {
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

inline std::pair<int, int> ratSub(std::pair<int, int> a, std::pair<int, int> b) {
    const long long n = (long long)a.first * b.second - (long long)b.first * a.second;
    const long long d = (long long)a.second * b.second;
    return ratNorm(n, d);
}

inline std::pair<int, int> ratAdd(std::pair<int, int> a, std::pair<int, int> b) {
    const long long n = (long long)a.first * b.second + (long long)b.first * a.second;
    const long long d = (long long)a.second * b.second;
    return ratNorm(n, d);
}

inline std::pair<int, int> ratMulInt(std::pair<int, int> a, int k) {
    return ratNorm((long long)a.first * k, a.second);
}

inline std::pair<int, int> ratDivInt(std::pair<int, int> a, int n) {
    if (n == 0)
        return {0, 1};
    return ratNorm(a.first, (long long)a.second * n);
}

// Line k: lower + k * (upper - lower) / nSteps; per-slot rationals; trim trailing zero exponents.
inline std::vector<std::pair<int, int>> edoVectorForK(int k, int nSteps,
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

inline std::vector<std::pair<int, int>> addVec(const std::vector<std::pair<int, int>>& a,
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

inline std::vector<std::pair<int, int>> subVec(const std::vector<std::pair<int, int>>& a,
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




// Dense exponents for primes 2,3,5,… ; empty vector → "0"; zero exponent prints as "0" not "(0/den)".
inline std::string formatPrimePowerVector(const std::vector<std::pair<int, int>>& v) {
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
inline bool regionHasDragDefinedEdoLattice(const Region* r) {
    return r && r->tuningEdoSubdivisionSteps > 0 &&
           (!r->tuningEdoLowerVector.empty() || !r->tuningEdoUpperVector.empty());
}

inline RegionTuningSnapshot captureRegionTuning(const Region* region) {
    RegionTuningSnapshot s;
    if (!region) return s;
    s.mode = region->tuningMode;
    s.harmonicAnchorNumber = region->tuningAnchorHarmonic;
    s.edoSubdivisionSteps = region->tuningEdoSubdivisionSteps;
    s.edoLowerVector = region->tuningEdoLowerVector;
    s.edoUpperVector = region->tuningEdoUpperVector;
    s.harmonicAnchorVector = region->tuningHarmonicAnchorVector;
    s.rhythmEdoSubdivisionSteps = region->rhythmEdoSubdivisionSteps;
    s.rhythmEdoLowerVector = region->rhythmEdoLowerVector;
    s.rhythmEdoUpperVector = region->rhythmEdoUpperVector;
    s.transportLines = region->transportLines;
    s.gridSpans = region->gridSpans;
    return s;
}

inline void applyRegionTuning(Region* region, const RegionTuningSnapshot& s) {
    if (!region) return;
    region->tuningMode = s.mode;
    region->tuningAnchorHarmonic = s.harmonicAnchorNumber;
    region->tuningEdoSubdivisionSteps = std::max(1, s.edoSubdivisionSteps);
    region->tuningEdoLowerVector = s.edoLowerVector;
    region->tuningEdoUpperVector = s.edoUpperVector;
    region->tuningHarmonicAnchorVector = s.harmonicAnchorVector;
    region->rhythmEdoSubdivisionSteps = s.rhythmEdoSubdivisionSteps;
    region->rhythmEdoLowerVector = s.rhythmEdoLowerVector;
    region->rhythmEdoUpperVector = s.rhythmEdoUpperVector;
    region->transportLines = s.transportLines;
    region->gridSpans = s.gridSpans;
}

inline json regionTuningSnapshotToUndoJson(const RegionTuningSnapshot& s) {
    json j;
    j["tuningMode"] = s.mode;
    j["tuningAnchorHarmonic"] = s.harmonicAnchorNumber;
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
    j["rhythmEdoSubdivisionSteps"] = s.rhythmEdoSubdivisionSteps;
    j["rhythmEdoLowerVector"] = json::array();
    for (const auto& pr : s.rhythmEdoLowerVector)
        j["rhythmEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["rhythmEdoUpperVector"] = json::array();
    for (const auto& pr : s.rhythmEdoUpperVector)
        j["rhythmEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    j["transportLines"] = latticeSpansToJSON(s.transportLines);
    j["gridSpans"] = latticeSpansToJSON(s.gridSpans);
    return j;
}

inline json noteTuningSnapshotToUndoJson(const NoteTuningSnapshot& s) {
    json j;
    j["tuningMode"] = s.tuningMode;
    j["tuningAnchorHarmonic"] = s.tuningAnchorHarmonic;
    j["tuningEdoSubdivisionSteps"] = s.tuningEdoSubdivisionSteps;
    j["tuningEdoLowerVector"] = json::array();
    for (const auto& pr : s.tuningEdoLowerVector)
        j["tuningEdoLowerVector"].push_back(json::array({pr.first, pr.second}));
    j["tuningEdoUpperVector"] = json::array();
    for (const auto& pr : s.tuningEdoUpperVector)
        j["tuningEdoUpperVector"].push_back(json::array({pr.first, pr.second}));
    return j;
}

inline NoteTuningSnapshot captureNoteTuning(const std::shared_ptr<Note>& n) {
    NoteTuningSnapshot s;
    if (!n) return s;
    s.tuningMode = n->tuningMode;
    s.tuningAnchorHarmonic = n->tuningAnchorHarmonic;
    s.tuningEdoSubdivisionSteps = n->tuningEdoSubdivisionSteps;
    s.tuningEdoLowerVector = n->tuningEdoLowerVector;
    s.tuningEdoUpperVector = n->tuningEdoUpperVector;
    return s;
}

inline void applyNoteTuningSnapshot(const std::shared_ptr<Note>& n, const NoteTuningSnapshot& s) {
    if (!n) return;
    n->tuningMode = s.tuningMode;
    n->tuningAnchorHarmonic = s.tuningAnchorHarmonic;
    n->tuningEdoSubdivisionSteps = s.tuningEdoSubdivisionSteps;
    n->tuningEdoLowerVector = s.tuningEdoLowerVector;
    n->tuningEdoUpperVector = s.tuningEdoUpperVector;
}

// ---------------------------------------------------------------------------
// Shared rhythm grid — used by both PianoRoll and SongRoll
// ---------------------------------------------------------------------------
struct RhythmGridLine {
    float seconds = 0.f;
    std::vector<std::pair<int, int>> integerPairs;
    // Thickness only: span boundaries are drawn thick.
    bool isBoundary = false;
    explicit RhythmGridLine(float s) : seconds(s) {}
};

/// Generate EDO rhythm lines within the given second range.
/// Expands outward from k=0 until both directions are beyond [minSec, maxSec].
inline void generateRhythmLines(std::vector<RhythmGridLine>& outLines,
                                std::vector<std::string>& outLabels,
                                int steps,
                                const std::vector<std::pair<int, int>>& lower,
                                const std::vector<std::pair<int, int>>& upper,
                                float minSec, float maxSec) {
    outLines.clear();
    outLabels.clear();
    if (steps <= 0) return;

    const float lowerSec = Note::beatsFromVector(lower);
    const float upperSec = Note::beatsFromVector(upper);
    const float stepSec = (upperSec - lowerSec) / static_cast<float>(steps);
    if (stepSec == 0.f) {
        if (lowerSec >= minSec && lowerSec <= maxSec) {
            auto pairs = lower;
            outLines.emplace_back(lowerSec);
            outLines.back().integerPairs = std::move(pairs);
            outLines.back().isBoundary = true;
            outLabels.push_back("0");
        }
        return;
    }

    const int kFirst = static_cast<int>(std::ceil((minSec - lowerSec) / stepSec)) - 1;
    const int kLast  = static_cast<int>(std::floor((maxSec - lowerSec) / stepSec)) + 1;

    for (int k = kFirst; k <= kLast; ++k) {
        auto pairs = edoVectorForK(k, steps, lower, upper);
        const float s = Note::beatsFromVector(pairs);
        outLines.emplace_back(s);
        outLines.back().integerPairs = std::move(pairs);
        outLines.back().isBoundary = (k % steps == 0);
        outLabels.push_back(std::to_string(k));
    }
}

/// Lines of one span over the given window. The span's spacing is what matters — the window
/// is clipped to the span's slice of the timeline, and the outermost spans run to infinity.
inline void appendSpanLines(std::vector<RhythmGridLine>& outLines,
                            std::vector<std::string>& outLabels,
                            const RhythmLatticeSpan& span, float loSec, float hiSec) {
    std::vector<RhythmGridLine> tmp;
    std::vector<std::string> tmpLabels;
    generateRhythmLines(tmp, tmpLabels, span.steps, span.lower, span.upper, loSec, hiSec);
    const float spanLo = Note::beatsFromVector(span.lower);
    const float spanHi = Note::beatsFromVector(span.upper);
    const float stepSec = (spanHi - spanLo) / static_cast<float>(std::max(1, span.steps));
    for (size_t i = 0; i < tmp.size(); ++i) {
        if (tmp[i].seconds < loSec - 0.0005f || tmp[i].seconds > hiSec + 0.0005f) continue;
        // The span-boundary pattern repeats every span length, so it carries on past the
        // span's own bounds — e.g. a span of 4 divisions over one beat marks every beat thick.
        if (stepSec > 0.0005f) {
            const long long k = std::llround((tmp[i].seconds - spanLo) / stepSec);
            tmp[i].isBoundary = (k % std::max(1, span.steps)) == 0;
        } else {
            tmp[i].isBoundary = true;
        }
        outLines.push_back(tmp[i]);
        outLabels.push_back(i < tmpLabels.size() ? tmpLabels[i] : std::string());
    }
}

/// The transport line set for a visible window: every span's lines, sorted and de-duplicated.
/// Spans partition the timeline, so each one only owns its slice; the first and last extend
/// their spacing outward without limit.
inline void generateTransportLines(std::vector<RhythmGridLine>& outLines,
                                   std::vector<std::string>& outLabels,
                                   const std::vector<RhythmLatticeSpan>& spans,
                                   float minSec, float maxSec) {
    // Creation order matters: index 0 is the base rate and later spans are overrides.
    const auto& sorted = spans;
    const size_t n = sorted.size();
    std::vector<float> lo(n), hi(n);
    for (size_t i = 0; i < n; ++i) {
        lo[i] = Note::beatsFromVector(sorted[i].lower);
        hi[i] = Note::beatsFromVector(sorted[i].upper);
    }

    auto spanAt = [&](float t) -> size_t {
        // A containing span owns t. Outside every span, the span to the LEFT governs (its
        // division and offset carry on); if there is nothing to the left, the first span does.
        for (size_t i = 0; i < n; ++i) {
            if (t >= lo[i] - 0.0005f && t <= hi[i] + 0.0005f)
                return i;
        }
        size_t left = SIZE_MAX;
        for (size_t i = 0; i < n; ++i) {
            if (lo[i] <= t && (left == SIZE_MAX || lo[i] > lo[left])) left = i;
        }
        if (left != SIZE_MAX) return left;
        size_t first = 0;
        for (size_t i = 1; i < n; ++i)
            if (lo[i] < lo[first]) first = i;
        return first;
    };

    std::vector<std::pair<RhythmGridLine, std::string>> all;
    for (size_t si = 0; si < n; ++si) {
        std::vector<RhythmGridLine> tmp;
        std::vector<std::string> tmpLabels;
        // Each span's series is evaluated across the whole window at its own rate and offset;
        // a line is kept only where this span is the one that owns that time.
        appendSpanLines(tmp, tmpLabels, sorted[si], minSec, maxSec);
        for (size_t i = 0; i < tmp.size(); ++i) {
            if (spanAt(tmp[i].seconds) != si) continue;
            all.emplace_back(tmp[i], i < tmpLabels.size() ? tmpLabels[i] : std::string());
        }
    }
    std::sort(all.begin(), all.end(),
        [](const std::pair<RhythmGridLine, std::string>& a,
           const std::pair<RhythmGridLine, std::string>& b) { return a.first.seconds < b.first.seconds; });

    outLines.clear();
    outLabels.clear();
    for (auto& e : all) {
        if (!outLines.empty() && std::fabs(outLines.back().seconds - e.first.seconds) < 0.0005f)
            continue;
        outLines.push_back(std::move(e.first));
        outLabels.push_back(std::move(e.second));
    }
}

/// One division of a span: (upper - lower) / steps, as an exact vector.
inline std::vector<std::pair<int, int>> latticeStepVec(const RhythmLatticeSpan& s) {
    auto d = subVec(s.upper, s.lower);
    for (auto& p : d) p = ratDivInt(p, std::max(1, s.steps));
    return d;
}

/// The span's grid point at or before `t`, so a trimmed remainder keeps the old rate AND offset.
inline std::vector<std::pair<int, int>> latticeAnchorAtOrBefore(const RhythmLatticeSpan& s,
                                                               const std::vector<std::pair<int, int>>& t) {
    const auto step = latticeStepVec(s);
    const float stepSec = Note::beatsFromVector(step);
    if (stepSec <= 0.0005f) return s.lower;
    const float delta = Note::beatsFromVector(t) - Note::beatsFromVector(s.lower);
    const long long k = static_cast<long long>(std::floor(delta / stepSec + 1e-4f));
    auto shift = step;
    for (auto& p : shift) p = ratMulInt(p, static_cast<int>(k));
    return addVec(s.lower, shift);
}

/// Default span for a fresh region / song roll: beat 0 to beat 1, divided in four.
inline RhythmLatticeSpan defaultLatticeSpan(int division) {
    RhythmLatticeSpan s;
    s.steps = std::max(1, division);
    s.lower = {};
    s.upper = {{1, 1}};
    return s;
}

/// The span's first grid point at or after `t`, so a right remainder keeps the old offset.
inline std::vector<std::pair<int, int>> latticeAnchorAtOrAfter(const RhythmLatticeSpan& s,
                                                              const std::vector<std::pair<int, int>>& t) {
    const auto step = latticeStepVec(s);
    const float stepSec = Note::beatsFromVector(step);
    if (stepSec <= 0.0005f) return s.lower;
    const float delta = Note::beatsFromVector(t) - Note::beatsFromVector(s.lower);
    const long long k = static_cast<long long>(std::ceil(delta / stepSec - 1e-4f));
    auto shift = step;
    for (auto& p : shift) p = ratMulInt(p, static_cast<int>(k));
    return addVec(s.lower, shift);
}

/// One division before `t` in the span's grid, used to make a one-period preservation span.
inline std::vector<std::pair<int, int>> latticeBefore(const RhythmLatticeSpan& s,
                                                      const std::vector<std::pair<int, int>>& t) {
    const auto step = latticeAnchorAtOrBefore(s, t);
    auto back = latticeStepVec(s);
    for (auto& p : back) p = ratMulInt(p, -1);
    return addVec(step, back);
}

/// Insert a span covering [lower, upper] into the partition. Overlapping spans are split into
/// left/right remainders that keep the old division and offset. Where the new span reaches
/// into territory that had no span, a one-period span of the old rate is left on each side so
/// the undefined region still evaluates exactly as it did before.
inline void insertLatticeSpan(std::vector<RhythmLatticeSpan>& spans, RhythmLatticeSpan span) {
    const float lo = Note::beatsFromVector(span.lower);
    const float hi = Note::beatsFromVector(span.upper);
    if (hi <= lo + 0.0005f) return;
    span.steps = std::max(1, span.steps);

    if (spans.empty()) {
        spans.push_back(std::move(span));
        return;
    }

    std::sort(spans.begin(), spans.end(), [](const RhythmLatticeSpan& a, const RhythmLatticeSpan& b) {
        return Note::beatsFromVector(a.lower) < Note::beatsFromVector(b.lower);
    });

    std::vector<RhythmLatticeSpan> kept;
    for (const auto& s : spans) {
        const float sLo = Note::beatsFromVector(s.lower);
        const float sHi = Note::beatsFromVector(s.upper);
        if (sHi <= lo + 0.0005f) { kept.push_back(s); continue; }
        if (sLo >= hi - 0.0005f) { kept.push_back(s); continue; }
        // Remainders keep the old step size, not the old count: trimming a span must not
        // re-divide it, so the number of divisions is recomputed from the length.
        const float stepSec = (sHi - sLo) / static_cast<float>(std::max(1, s.steps));
        if (sLo < lo - 0.0005f) {
            RhythmLatticeSpan left = s;
            left.upper = span.lower;
            left.steps = std::max(1, static_cast<int>(std::lround((lo - sLo) / stepSec)));
            kept.push_back(std::move(left));
        }
        if (sHi > hi + 0.0005f) {
            RhythmLatticeSpan right = s;
            right.lower = latticeAnchorAtOrAfter(s, span.upper);
            const float rightLen = sHi - Note::beatsFromVector(right.lower);
            right.steps = std::max(1, static_cast<int>(std::lround(rightLen / stepSec)));
            kept.push_back(std::move(right));
        }
    }

    kept.push_back(span);
    spans = std::move(kept);
    std::sort(spans.begin(), spans.end(), [](const RhythmLatticeSpan& a, const RhythmLatticeSpan& b) {
        return Note::beatsFromVector(a.lower) < Note::beatsFromVector(b.lower);
    });
}

/// Find the index of the rhythm line closest to the given seconds value.
inline size_t closestRhythmLineIndexForSeconds(const std::vector<RhythmGridLine>& lines, float seconds) {
    if (lines.empty()) return SIZE_MAX;
    size_t best = 0;
    float bd = FLT_MAX;
    for (size_t i = 0; i < lines.size(); ++i) {
        const float d = std::fabs(lines[i].seconds - seconds);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

struct TimeBoundary {
    float seconds = 0.f;
    std::vector<std::pair<int, int>> pairs;
};

/// Nearest boundary among grid rhythm lines, transport lines, and other notes'
/// start/end lines (the edited note is excluded). No priority: nearest wins.
inline bool closestTimeBoundary(const std::vector<RhythmGridLine>& rhythm,
                                const std::vector<RhythmGridLine>& transport,
                                const std::vector<std::shared_ptr<Note>>* notes,
                                int excludeNoteID,
                                float seconds,
                                TimeBoundary& out) {
    float bestDist = FLT_MAX;
    bool found = false;
    auto consider = [&](float s, const std::vector<std::pair<int, int>>& pairs) {
        const float d = std::fabs(s - seconds);
        if (d < bestDist) {
            bestDist = d;
            out.seconds = s;
            out.pairs = pairs;
            found = true;
        }
    };
    for (const auto& line : rhythm)
        consider(line.seconds, line.integerPairs);
    for (const auto& line : transport)
        consider(line.seconds, line.integerPairs);
    if (notes) {
        for (const auto& note : *notes) {
            if (!note || static_cast<int>(note->id) == excludeNoteID) continue;
            consider(Note::beatsFromVector(note->rhythmVector), note->rhythmVector);
            consider(Note::beatsFromVector(note->rhythmEndVector), note->rhythmEndVector);
        }
    }
    return found;
}

/// Nearest line across the grid rhythm set and the transport-strip set.
/// Grid lines win ties; the transport set only replaces one when strictly closer.
inline const RhythmGridLine* closestRhythmOrTransportLine(const std::vector<RhythmGridLine>& rhythm,
                                                          const std::vector<RhythmGridLine>& transport,
                                                          float seconds) {
    const RhythmGridLine* best = nullptr;
    float bd = FLT_MAX;
    for (const auto& line : rhythm) {
        const float d = std::fabs(line.seconds - seconds);
        if (d < bd) { bd = d; best = &line; }
    }
    for (const auto& line : transport) {
        const float d = std::fabs(line.seconds - seconds);
        if (d < bd) { bd = d; best = &line; }
    }
    return best;
}

