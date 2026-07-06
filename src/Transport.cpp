#include "Transport.h"
#include "GridView.h"
#include "Project.h"
#include "Button.h"
#include "PianoRollInternal.h"
#include <functional>
#include <algorithm>

namespace {
constexpr float kBeatLineW = 2.f;

double snappedSec(GridView* view, float mouseX) {
    float rawSec = static_cast<float>((mouseX + view->scrollX - view->leftMargin) / view->dW);
    rawSec = view->adjustTransportSeekSec(rawSec);
    size_t idx = closestRhythmLineIndexForSeconds(view->rhythmLines, rawSec);
    if (idx != SIZE_MAX && idx < view->rhythmLines.size())
        return static_cast<double>(view->rhythmLines[idx].seconds);
    return static_cast<double>(rawSec);
}
} // namespace

Transport::Transport(GridView* view) : view(view), dstRect(view->dstRect) {
}

void Transport::moveMouse(float mouseX, float mouseY) {
    this->mouseX = mouseX;
    this->mouseY = mouseY;
}

void Transport::clickMouse() {
    const float barY = view->topMargin - Transport::kTimelineHeight;
    if (mouseY >= barY && mouseY <= barY + Transport::kTimelineHeight) {
        draggingTimeline = true;
        double sec = snappedSec(view, mouseX);
        view->project->effectiveTime.store(sec);
        view->project->timeSeconds.store(sec);
        view->project->playHeadStart = fract(static_cast<int>(sec * 1000000), 1000000);
    }
}

void Transport::handleMotion() {
    if (!draggingTimeline) return;
    double sec = snappedSec(view, mouseX);
    view->project->effectiveTime.store(sec);
    view->project->timeSeconds.store(sec);
    view->project->playHeadStart = fract(static_cast<int>(sec * 1000000), 1000000);
}

Transport::~Transport() {
}

void Transport::render(SDL_Renderer* renderer) {
    const float barY = view->dstRect->y + view->topMargin - Transport::kTimelineHeight;

    // Top portion (background)
    uint8_t bgColor[4] = {50, 50, 50, 255};
    SDL_SetRenderDrawColor(renderer, bgColor[0], bgColor[1], bgColor[2], bgColor[3]);
    SDL_FRect topRect{view->dstRect->x, view->dstRect->y, view->dstRect->w, view->topMargin - Transport::kTimelineHeight};
    SDL_RenderFillRect(renderer, &topRect);

    // Timeline bar background (slightly darker)
    uint8_t barColor[4] = {35, 35, 40, 255};
    SDL_SetRenderDrawColor(renderer, barColor[0], barColor[1], barColor[2], barColor[3]);
    SDL_FRect barRect{view->dstRect->x, barY, view->dstRect->w, Transport::kTimelineHeight};
    SDL_RenderFillRect(renderer, &barRect);

    // Rhythm grid tick marks
    for (const auto& rl : view->rhythmLines) {
        float x = view->dstRect->x + view->getX(rl.seconds);
        if (x < view->dstRect->x || x > view->dstRect->x + view->dstRect->w) continue;
        if (rl.isBeat) {
            SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
            SDL_FRect lineRect{x - kBeatLineW / 2.f, barY, kBeatLineW, Transport::kTimelineHeight};
            SDL_RenderFillRect(renderer, &lineRect);
        } else {
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
            SDL_RenderLine(renderer, x, barY, x, barY + Transport::kTimelineHeight);
        }
    }
}
