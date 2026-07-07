#include "AutomationCurve.h"
#include "Note.h"
#include <algorithm>
#include <cmath>

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
    if (relSec <= Note::secondsFromVector(points.front().timeVec)) return points.front().v;
    if (relSec >= Note::secondsFromVector(points.back().timeVec))  return points.back().v;
    size_t lo = 0, hi = points.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (Note::secondsFromVector(points[mid].timeVec) <= relSec)
            lo = mid;
        else
            hi = mid;
    }
    const auto& a = points[lo];
    const auto& b = points[lo + 1];
    float aRel = Note::secondsFromVector(a.timeVec);
    float bRel = Note::secondsFromVector(b.timeVec);
    float dt = bRel - aRel;
    if (dt <= 0.f) return a.v;
    return evalCurveSegment(a, b, (relSec - aRel) / dt);
}

float AutomationCurve::evaluate(float t) const {
    if (points.empty()) return 0.f;
    float first = Note::secondsFromVector(points.front().timeVec);
    float last  = Note::secondsFromVector(points.back().timeVec);
    return evalAtRelSec(points, first + t * (last - first));
}

float AutomationCurve::evaluateAtSec(float sec, float startSec, float) const {
    if (points.empty()) return 0.f;
    return evalAtRelSec(points, sec - startSec);
}

void AutomationCurve::addPoint(const std::vector<std::pair<int,int>>& timeVec, float v, CurveShape::Type shapeType) {
    CurvePoint pt{timeVec, v, {shapeType, 0.f}};
    float ptSec = Note::secondsFromVector(timeVec);
    auto it = std::lower_bound(points.begin(), points.end(), ptSec,
        [](const CurvePoint& a, float s) { return Note::secondsFromVector(a.timeVec) < s; });
    points.insert(it, std::move(pt));
}

void AutomationCurve::removePoint(size_t idx) {
    if (idx < points.size())
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(idx));
}
