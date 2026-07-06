#include "GridView.h"
#include "Playhead.h"
#include "SDL_Events.h"
#include "Transport.h"
#include "WindowHandler.h"

GridView::GridView(SDL_FRect* rect, float leftMargin, Window* w, Project* p) :
    leftMargin(leftMargin),
    isShiftPressed(WindowHandler::instance()->isShiftPressed),
    isAltPressed(WindowHandler::instance()->isAltPressed),
    isCtrlPressed(WindowHandler::instance()->isCtrlPressed),
    project(p)
{

    if(rect != nullptr) {
        dstRect = rect;
    } else {
        dstRect = new SDL_FRect{
            0,
            0,
            1000,
            600
        };
    }

    this->width = dstRect->w;
    this->height = dstRect->h;
    this->x = dstRect->x;
    this->y = dstRect->y;

    window = w->window;
    renderer = w->renderer;

    this->playHead = new Playhead(&gridRect, dstRect, project);
    this->transport = new Transport(this);
}

void GridView::createGridRect() {
    gridRect = {
        dstRect->x+leftMargin,
        dstRect->y+topMargin,
        dstRect->w-leftMargin-rightMargin,
        dstRect->h - topMargin - bottomMargin
    };
}

GridView::~GridView() {
    delete playHead;
    delete transport;
}

bool GridView::tick(SDL_Renderer* renderer) {
    moveMouse();
    customTick(renderer);
    return true;
}


bool GridView::handleInput(SDL_Event& e) {

    bool running = true;
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            transport->handleMotion();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            transport->clickMouse();
            clickMouse(e);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            transport->draggingTimeline = false;
            clickMouse(e);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (mouseX < gridRect.x || mouseX >= (gridRect.x + gridRect.w) ||
                mouseY < gridRect.y || mouseY >= (gridRect.y + gridRect.h)) {
                break;
            }
            if (isCtrlPressed) {
                double gridAtX = (mouseX + scrollX - leftMargin) / dW;
                dW *= std::pow(scaleSensitivity, e.wheel.y);
                if (dW <= 4) {
                    dW = 4;
                }
                UpdateGrid();
                scrollX = gridAtX * dW - mouseX + leftMargin;
            } else
                if (isAltPressed) {
                    double gridAtY = (mouseY + scrollY - topMargin)/ divHeight;
                    divHeight *= std::pow(scaleSensitivity, e.wheel.y);
                    if (divHeight < height * minHeight || divHeight <= 0) {
                        divHeight = height * minHeight;
                    }
                    UpdateGrid();
                    scrollY = gridAtY * divHeight - mouseY + topMargin;
                } else if (isShiftPressed) {
                    scrollX += e.wheel.y * scrollSensitivity;
                } else {
                    scrollY -= e.wheel.y * scrollSensitivity;  // Adjust scroll amount based on mouse wheel
                }

        case SDL_EVENT_KEY_DOWN:
            switch (e.key.scancode) {
                case SDL_SCANCODE_SPACE:
                    break;
                default:
                    break;
            }
            break;

        case SDL_EVENT_QUIT:
            running =  false;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            running = false;
            break;
        case SDL_EVENT_DROP_BEGIN:
            dropping = true;
            beginDrop(e.drop);
            break;
        case SDL_EVENT_DROP_COMPLETE:
            dropping = false;
            break;
        case SDL_EVENT_DROP_POSITION:
            break;
        case SDL_EVENT_DROP_FILE:
            dropFile(e.drop);
            break;
        default:
            break;
    }

    handleCustomInput(e);

    return true;
}

void GridView::moveMouse() {
    transport->moveMouse(mouseX, mouseY);
}


void GridView::RenderGridTexture(SDL_Renderer* renderer) {
    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);

    setRenderColor(renderer, colors.grid);

    for(auto line : times) {
        float val = getX(line);
        SDL_RenderLine(renderer, val, 0, val, height);
    }

    for (auto line : lines) {
        float val = getY(line);
        SDL_RenderLine(renderer, 0, val, width, val);
    }
    SDL_SetRenderTarget(renderer, target);
}

void GridView::renderRhythmIntervalPreviewBand(SDL_Renderer* renderer, float startSec, float endSec) {
    const float xLeft = std::min(getX(startSec), getX(endSec));
    const float xRight = std::max(getX(startSec), getX(endSec));
    SDL_FRect band{dstRect->x + xLeft, dstRect->y + topMargin, std::max(1.0f, xRight - xLeft), height - topMargin - bottomMargin};
    SDL_SetRenderDrawColor(renderer, 45, 110, 210, 32);
    SDL_RenderFillRect(renderer, &band);
    SDL_SetRenderDrawColor(renderer, 70, 150, 235, 78);
    SDL_RenderRect(renderer, &band);
}

void GridView::renderRhythmIntervalEndLine(SDL_Renderer* renderer, float endSec) {
    const float xEnd = getX(endSec);
    SDL_SetRenderDrawColor(renderer, 65, 190, 240, 128);
    SDL_RenderLine(renderer, dstRect->x + xEnd, dstRect->y + topMargin, dstRect->x + xEnd, dstRect->y + height - bottomMargin);
}

void GridView::setRenderColor(SDL_Renderer* renderer, uint8_t code[4]) {
    SDL_SetRenderDrawColor(renderer,
                           code[0],
                           code[1],
                           code[2],
                           code[3]
    );
}

void GridView::toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar) {

    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        if (e.key.scancode == keycode) {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                keyVar = true;
            } else if (e.type == SDL_EVENT_KEY_UP) {
                keyVar = false;
            }
        }
    }
}

float GridView::getHoveredTimeSec() {
    return (mouseX + scrollX - leftMargin) / dW;
}

float GridView::getX(float time) {
    return time * dW + leftMargin - scrollX;
}
