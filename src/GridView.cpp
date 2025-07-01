#include "GridView.h"
#include "Playhead.h"

GridView::GridView(bool detached, SDL_FRect* rect) : detached(detached) {

    this->playHead = new Playhead(&gridRect);

    if(rect != nullptr) {
        dstRect = rect;
    } else {
        dstRect = new SDL_FRect{
            0,
            0,
            800,
            600
        };
    }

    this->width = dstRect->w;
    this->height = dstRect->h;
    this->x = dstRect->x;
    this->y = dstRect->y;


    if(detached) {
        window = SDL_CreateWindow("Piano Roll", width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);

        renderer = SDL_CreateRenderer(window, NULL);
    } else {
        renderer = Home::get()->renderer;
    }
}


GridView::~GridView() {
    delete playHead;
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
}

void GridView::handleInput(SDL_Event& e) {
    toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
    toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
    toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);

    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            moveMouse();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            clickMouse(e);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            clickMouse(e);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (isCtrlPressed) {
                barWidth *= std::pow(scaleSensitivity, e.wheel.y);
                if (barWidth <= 4) {
                    barWidth = 4;
                }
                double gridAtX = (mouseX / cellWidth) + (scrollX / cellWidth);
                UpdateGrid();
                scrollX = gridAtX * cellWidth - mouseX;
            } else
                if (isAltPressed) {
                    divHeight *= std::pow(scaleSensitivity, e.wheel.y);
                    if (divHeight < height * 12 / 128 || divHeight <= 0) {
                        divHeight = height * 12 / 128;
                    }

                    double gridAtY = (mouseY / cellHeight) + (scrollY / cellHeight);
                    UpdateGrid();
                    scrollY = gridAtY * cellHeight - mouseY;
                } else if (isShiftPressed) {
                    scrollX += e.wheel.y * scrollSensitivity;
                } else {
                    scrollY -= e.wheel.y * scrollSensitivity;  // Adjust scroll amount based on mouse wheel
                }

        case SDL_EVENT_KEY_DOWN:
            switch (e.key.scancode) {
                case SDL_SCANCODE_SPACE:
                    project->togglePlaying();
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    handleCustomInput(e);
}

void GridView::moveMouse() {
    SDL_GetMouseState(&mouseX, &mouseY);
    mouseX -= gridRect.x + dstRect->x;
    mouseY -= dstRect->y;
}

void GridView::setRenderColor(SDL_Renderer* theRenderer, uint8_t code[4]) {
    SDL_SetRenderDrawColor(theRenderer,
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

fract GridView::getHoveredTime() {
    return fract(std::floor((mouseX+scrollX)/cellWidth),notesPerBar);
}
