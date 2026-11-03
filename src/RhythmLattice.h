#pragma once
// Shared rhythm-lattice types: the single-span structure the rhythm EDO uses, and the
// transport line set, which is a partition of the timeline into those spans.
#include <algorithm>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Divides [lower, upper] into `steps` equal parts. Times are prime-exponent vectors, so the
// positions stay exact; outside the span the spacing repeats.
struct RhythmLatticeSpan {
    int steps = 1;
    std::vector<std::pair<int, int>> lower;
    std::vector<std::pair<int, int>> upper = {{1, 1}};

    bool operator==(const RhythmLatticeSpan& o) const {
        return steps == o.steps && lower == o.lower && upper == o.upper;
    }
    bool operator!=(const RhythmLatticeSpan& o) const { return !(*this == o); }
};

inline std::vector<std::pair<int, int>> pairsFromJSON(const json& el) {
    std::vector<std::pair<int, int>> v;
    if (el.is_array())
        for (const auto& p : el)
            if (p.is_array() && p.size() >= 2)
                v.push_back({p[0].get<int>(), p[1].get<int>()});
    return v;
}

inline json pairsToJSON(const std::vector<std::pair<int, int>>& v) {
    json a = json::array();
    for (const auto& pr : v)
        a.push_back(json::array({pr.first, pr.second}));
    return a;
}

inline json latticeSpansToJSON(const std::vector<RhythmLatticeSpan>& spans) {
    json a = json::array();
    for (const auto& s : spans) {
        json o;
        o["steps"] = s.steps;
        o["lower"] = pairsToJSON(s.lower);
        o["upper"] = pairsToJSON(s.upper);
        a.push_back(std::move(o));
    }
    return a;
}

inline std::vector<RhythmLatticeSpan> latticeSpansFromJSON(const json& j, const char* key) {
    std::vector<RhythmLatticeSpan> out;
    if (j.contains(key) && j[key].is_array()) {
        for (const auto& o : j[key]) {
            if (!o.is_object()) continue;
            RhythmLatticeSpan s;
            s.steps = std::max(1, o.value("steps", 1));
            s.lower = pairsFromJSON(o.value("lower", json::array()));
            s.upper = pairsFromJSON(o.value("upper", json::array()));
            if (s.upper.empty()) s.upper = {{1, 1}};
            out.push_back(std::move(s));
        }
    }
    return out;
}
