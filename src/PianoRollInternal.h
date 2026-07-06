#pragma once
// Shared internals for the PianoRoll implementation files (PianoRoll.cpp,
// PianoRollLattice.cpp, PianoRollInput.cpp): tuning snapshots and prime-power
// rational vector math. Not part of the public PianoRoll API.
#include <vector>
#include <string>
#include <sstream>
#include <cfloat>
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
    bool isBeat = false;
    explicit RhythmGridLine(float s) : seconds(s) {}
};

/// Generate EDO rhythm lines within the given second range.
inline void generateRhythmLines(std::vector<RhythmGridLine>& outLines,
                                std::vector<std::string>& outLabels,
                                int steps,
                                const std::vector<std::pair<int, int>>& lower,
                                const std::vector<std::pair<int, int>>& upper,
                                float minSec = -60.f, float maxSec = 3600.f) {
    outLines.clear();
    outLabels.clear();
    if (steps <= 0) return;
    for (int k = -1024; k <= 1024; ++k) {
        auto pairs = edoVectorForK(k, steps, lower, upper);
        const float seconds = Note::secondsFromVector(pairs);
        if (seconds < minSec || seconds > maxSec) continue;
        outLines.emplace_back(seconds);
        outLines.back().integerPairs = std::move(pairs);
        outLines.back().isBeat = (steps > 0 && k % steps == 0);
        outLabels.push_back(std::to_string(k));
    }
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

