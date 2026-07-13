#include "AutomationCurve.h"
#include "Note.h"
#include <algorithm>
#include <cmath>
#include <functional>

AutomationCurve::AutomationCurve(Project* p, ArrangerNode* n) : GridElement(p, n) {
    type = ElementType::automationCurve;
}

AutomationCurve::~AutomationCurve() {}

void AutomationCurve::draw(SDL_Renderer*, float, int) {}

json AutomationCurve::toJSON() {
    json j;
    j["name"] = name;
    j["id"] = id;
    j["type"] = ElementType::automationCurve;
    j["targetParamType"] = targetParamType;
    j["valueRangeMin"] = valueRangeMin;
    j["valueRangeMax"] = valueRangeMax;
    j["points"] = json::array();
    for (const auto& pt : points) {
        json jpt;
        jpt["timeVec"] = json::array();
        for (const auto& pr : pt.timeVec)
            jpt["timeVec"].push_back(json::array({pr.first, pr.second}));
        jpt["v"] = pt.v;
        jpt["shape"] = static_cast<int>(pt.shape.type);
        jpt["shapeParam"] = pt.shape.param;
        j["points"].push_back(jpt);
    }
    j["positions"] = GridElement::toJSON();
    return j;
}

void AutomationCurve::fromJSON(json j) {
    id = j["id"];
    name = j.value("name", "Automation");
    targetParamType = j.value("targetParamType", 0);
    valueRangeMin = j.value("valueRangeMin", 0.f);
    valueRangeMax = j.value("valueRangeMax", 1.f);
    points.clear();
    if (j.contains("points") && j["points"].is_array()) {
        for (const auto& el : j["points"]) {
            CurvePoint pt;
            pt.v = el.value("v", 0.f);
            pt.shape.type = static_cast<CurveShape::Type>(el.value("shape", static_cast<int>(CurveShape::Single)));
            pt.shape.param = el.value("shapeParam", 0.f);
            if (el.contains("timeVec") && el["timeVec"].is_array()) {
                for (const auto& pr : el["timeVec"])
                    if (pr.is_array() && pr.size() >= 2)
                        pt.timeVec.push_back({pr[0].get<int>(), pr[1].get<int>()});
            }
            // Backcompat: old float t format
            if (pt.timeVec.empty() && el.is_array() && el.size() >= 3) {
                pt.v = el[1].get<float>();
                pt.shape.type = static_cast<CurveShape::Type>(el[2].get<int>());
                pt.timeVec = std::vector<std::pair<int,int>>{{1, 1}};
            }
            points.push_back(std::move(pt));
        }
    }
    if (j.contains("positions") && j["positions"].is_array())
        GridElement::fromJSON(j["positions"]);
}

static float evalCurveSegment(const CurvePoint& a, const CurvePoint& b, float local) {
    switch (a.shape.type) {
    case CurveShape::Hold: return a.v;
    case CurveShape::Single: {
        float p = a.shape.param;
        float t;
        if (p >= 0.f)
            t = local + ((1.f - std::pow(1.f - local, 1.f + p * 3.f)) - local) * (p > 0.f ? 1.f : 0.f);
        else
            t = local + (std::pow(local, 1.f - p * 3.f) - local) * (-p);
        if (std::fabs(p) < 0.01f) t = local;
        return a.v + (b.v - a.v) * t;
    }
    case CurveShape::Double: {
        float s = local * local * (3.f - 2.f * local);
        float t = local + (s - local) * std::fabs(a.shape.param) * 3.f;
        if (a.shape.param < 0.f) t = local + (local - t);
        return a.v + (b.v - a.v) * t;
    }
    }
    return a.v;
}

static float evalAtRelSec(const std::vector<CurvePoint>& points, float relSec) {
    if (relSec <= Note::beatsFromVector(points.front().timeVec)) return points.front().v;
    if (relSec >= Note::beatsFromVector(points.back().timeVec))  return points.back().v;
    size_t lo = 0, hi = points.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (Note::beatsFromVector(points[mid].timeVec) <= relSec)
            lo = mid;
        else
            hi = mid;
    }
    const auto& a = points[lo];
    const auto& b = points[lo + 1];
    float aRel = Note::beatsFromVector(a.timeVec);
    float bRel = Note::beatsFromVector(b.timeVec);
    float dt = bRel - aRel;
    if (dt <= 0.f) return a.v;
    return evalCurveSegment(a, b, (relSec - aRel) / dt);
}

// -- Adaptive Simpson quadrature ------------------------------------------------

static float adaptiveSimpson(float a, float b, float fa, float fm, float fb,
                              const std::function<float(float)>& f, float tol, int depth) {
    float h = (b - a) * 0.5f;
    float fl = f(a + h * 0.5f);
    float fr = f(b - h * 0.5f);
    float sLeft  = (h / 6.f) * (fa + 4.f * fl + fm);
    float sRight = (h / 6.f) * (fm + 4.f * fr + fb);
    float sTotal = sLeft + sRight;
    float sSimple = (b - a) / 6.f * (fa + 4.f * fm + fb);
    if (depth >= 10 || std::fabs(sTotal - sSimple) < tol)
        return sTotal;
    return adaptiveSimpson(a, a + h, fa, fl, fm, f, tol * 0.5f, depth + 1) +
           adaptiveSimpson(a + h, b, fm, fr, fb, f, tol * 0.5f, depth + 1);
}

static float integrateFunctor(float a, float b,
                               const std::function<float(float)>& f, float tol = 1e-7f) {
    if (b - a <= 0.f) return 0.f;
    float fa = f(a), fb = f(b), fm = f((a + b) * 0.5f);
    return adaptiveSimpson(a, b, fa, fm, fb, f, tol, 0);
}

// Integrate curve value over a single segment [x_a, x_b].
// -- Public integration methods -----------------------------------------------

float AutomationCurve::evaluateAtX(float x) const {
    if (points.empty()) return 0.f;
    return evalAtRelSec(points, x);
}

// -- Segment-based seconds-from-beats integration -----------------------------

// Integrate 60/evalCurveSegment over a single segment's beat span [x_a, x_b].
static float segmentSeconds(const CurvePoint& a, const CurvePoint& b, float x_a, float x_b) {
    float dx = x_b - x_a;
    if (dx <= 0.f) return 0.f;

    switch (a.shape.type) {
    case CurveShape::Hold:
        return 60.f / a.v * dx;

    case CurveShape::Single:
    case CurveShape::Double:
    default: {
        // Linear case: exact integral of 60 / (a.v + slope * local)
        if (std::fabs(a.shape.param) < 0.01f && std::fabs(b.v - a.v) > 1e-6f) {
            float slope = (b.v - a.v) / dx;
            return 60.f / slope * std::log(b.v / a.v);
        }
        if (std::fabs(a.shape.param) < 0.01f)
            return 60.f / a.v * dx;  // constant

        // Nonlinear: adaptive Simpson
        auto fn = [&](float x) -> float {
            float v = evalCurveSegment(a, b, (x - x_a) / dx);
            return 60.f / v;
        };
        return integrateFunctor(x_a, x_b, fn);
    }
    }
}

float AutomationCurve::secondsForBeats(float fromBeat, float toBeat) const {
    if (points.empty())
        return (toBeat - fromBeat) * 60.f / 120.f;
    if (fromBeat > toBeat) return -secondsForBeats(toBeat, fromBeat);
    if (fromBeat == toBeat) return 0.f;

    float firstBeat = Note::beatsFromVector(points.front().timeVec);
    float lastBeat  = Note::beatsFromVector(points.back().timeVec);

    float a = fromBeat, b = toBeat;
    float total = 0.f;

    // Before first point: flat extension
    if (a < firstBeat) {
        float endPre = std::min(b, firstBeat);
        total += 60.f / points.front().v * (endPre - a);
        a = endPre;
    }

    // Iterate segments
    if (a < lastBeat && points.size() >= 2) {
        size_t lo = 0, hi = points.size() - 1;
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (Note::beatsFromVector(points[mid].timeVec) <= a) lo = mid;
            else hi = mid;
        }

        // Partial first segment
        float x_lo = Note::beatsFromVector(points[lo].timeVec);
        float x_hi = Note::beatsFromVector(points[lo + 1].timeVec);
        if (a < x_hi) {
            float end = std::min(b, x_hi);
            const auto& pa = points[lo]; const auto& pb = points[lo + 1];
            float dx = x_hi - x_lo;
            if (pa.shape.type == CurveShape::Hold
                || (pa.shape.type != CurveShape::Hold && std::fabs(pa.shape.param) < 0.01f)) {
                // Hold or linear: virtual endpoints are exact
                float locA = (a - x_lo) / dx, locB = (end - x_lo) / dx;
                CurvePoint aAt = pa; aAt.v = evalCurveSegment(pa, pb, locA);
                CurvePoint bAt = pb; bAt.v = evalCurveSegment(pa, pb, locB);
                total += segmentSeconds(aAt, bAt, a, end);
            } else {
                // Nonlinear: integrate on original segment with correct local range
                auto fn = [&](float x) -> float {
                    return 60.f / evalCurveSegment(pa, pb, (x - x_lo) / dx);
                };
                total += integrateFunctor(a, end, fn);
            }
            if (b <= x_hi) return total;
            lo++;
        }

        // Full middle segments
        while (lo + 1 < points.size()) {
            x_lo = Note::beatsFromVector(points[lo].timeVec);
            x_hi = Note::beatsFromVector(points[lo + 1].timeVec);
            if (b <= x_hi) {
                const auto& pa = points[lo]; const auto& pb = points[lo + 1];
                float dx = x_hi - x_lo;
                if (pa.shape.type == CurveShape::Hold
                    || (pa.shape.type != CurveShape::Hold && std::fabs(pa.shape.param) < 0.01f)) {
                    float locEnd = (b - x_lo) / dx;
                    CurvePoint bAt = pb; bAt.v = evalCurveSegment(pa, pb, locEnd);
                    total += segmentSeconds(pa, bAt, x_lo, b);
                } else {
                    auto fn = [&](float x) -> float {
                        return 60.f / evalCurveSegment(pa, pb, (x - x_lo) / dx);
                    };
                    total += integrateFunctor(x_lo, b, fn);
                }
                return total;
            }
            total += segmentSeconds(points[lo], points[lo + 1], x_lo, x_hi);
            lo++;
        }
    }

    // After last point: flat extension
    if (b > lastBeat)
        total += 60.f / points.back().v * (b - std::max(a, lastBeat));

    return total;
}

float AutomationCurve::evaluate(float t) const {
    if (points.empty()) return 0.f;
    float first = Note::beatsFromVector(points.front().timeVec);
    float last  = Note::beatsFromVector(points.back().timeVec);
    return evalAtRelSec(points, first + t * (last - first));
}

float AutomationCurve::evaluateAtSec(float sec, float startSec, float) const {
    if (points.empty()) return 0.f;
    return evalAtRelSec(points, sec - startSec);
}

void AutomationCurve::addPoint(const std::vector<std::pair<int,int>>& timeVec, float v, CurveShape::Type shapeType) {
    CurvePoint pt{timeVec, v, {shapeType, 0.f}};
    float ptSec = Note::beatsFromVector(timeVec);
    auto it = std::lower_bound(points.begin(), points.end(), ptSec,
        [](const CurvePoint& a, float s) { return Note::beatsFromVector(a.timeVec) < s; });
    points.insert(it, std::move(pt));
}

void AutomationCurve::removePoint(size_t idx) {
    if (idx < points.size())
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(idx));
}
