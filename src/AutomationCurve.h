#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <utility>
#include <cstdint>
#include "GridElement.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

struct CurveShape {
    enum Type : uint8_t { Hold, Single, Double };
    Type type = Single;
    float param = 0.f;  // bow for Single (-1..1), balance for Double (-1..1)
};

struct CurvePoint {
    std::vector<std::pair<int,int>> timeVec;
    float v = 0.f;
    CurveShape shape;
};

class AutomationCurve : public GridElement {

public:
    AutomationCurve(Project*, ArrangerNode*);
    ~AutomationCurve() override;

    std::string name = "Automation";

    void draw(SDL_Renderer*, float, int) override;

    json toJSON() override;
    void fromJSON(json) override;

    std::vector<CurvePoint> points;
    int targetParamType = 0;
    float valueRangeMin = 0.f;
    float valueRangeMax = 1.f;

    float evaluate(float t) const;
    float evaluateAtSec(float sec, float startSec, float endSec) const;
    float evaluateAtX(float x) const;

    // ∫ 60/BPM(b) db over [fromBeat, toBeat] — seconds elapsed
    float secondsForBeats(float fromBeat, float toBeat) const;

    void addPoint(const std::vector<std::pair<int,int>>& timeVec, float v,
                   CurveShape::Type shapeType = CurveShape::Single);
    void removePoint(size_t idx);
};
