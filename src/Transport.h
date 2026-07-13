#pragma once
#include <SDL3/SDL.h>
#include <vector>
#include <utility>

class Project;
class GridView;
class CurveEditor;

class Transport {
public:
    static constexpr float kTimelineHeight = 18.f;

    Transport(GridView*);
    ~Transport();

    GridView* view;
    SDL_FRect* dstRect;

    int mouseX;
    int mouseY;

    void render(SDL_Renderer*);

    void moveMouse(float, float);
    void clickMouse(uint8_t button = 0);
    void handleMotion();

    bool draggingTimeline = false;
    void finishTempoDrag();

    CurveEditor* tempoCurveEditor = nullptr;

private:
    void updateTempoCurveBounds();
    void handleTempoMouseDown(float areaH);
    void handleTempoMouseMove(float areaH);

    std::vector<std::pair<int,int>> tempoStartVec;
    std::vector<std::pair<int,int>> tempoEndVec;
    std::vector<std::pair<int,int>> tempoOffsetVec;

    struct TempoDragState;
    TempoDragState* dragState = nullptr;
};
