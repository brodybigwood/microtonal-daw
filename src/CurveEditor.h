#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <utility>

class AutomationCurve;
class GridView;

class CurveEditor {
public:
    CurveEditor(
        AutomationCurve* curve,
        GridView* gridView,
        const std::vector<std::pair<int,int>>* startVec,
        const std::vector<std::pair<int,int>>* endVec,
        const std::vector<std::pair<int,int>>* offsetVec
    );

    SDL_FRect area{0,0,0,0};
    int hoveredPointIdx = -1;
    int draggingPointIdx = -1;
    int hoveredTensionSeg = -1;

    void updateArea(float y, float h);

    void render(SDL_Renderer*);
    int hitTestPoint(float mx, float my);
    int hitTestTension(float mx, float my);
    void tensionHandlePos(size_t seg, float& outX, float& outY);
    float hitTestCurve(float mx, float my);

    AutomationCurve* curve() { return curve_; }
    GridView* gridView() { return gridView_; }

    float startSec() const;
    float endSec() const;
    float contentOriginSec() const;
    const std::vector<std::pair<int,int>>* startVec() const { return startVec_; }
    const std::vector<std::pair<int,int>>* offsetVec() const { return offsetVec_; }

    static constexpr float kHandleRadius = 7.f;

private:
    AutomationCurve* curve_;
    GridView* gridView_;
    const std::vector<std::pair<int,int>>* startVec_;
    const std::vector<std::pair<int,int>>* endVec_;
    const std::vector<std::pair<int,int>>* offsetVec_;

    float tx(const std::vector<std::pair<int,int>>& timeVec) const;
    float ty(float v) const;
};
