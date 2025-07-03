#include "GridView.h"
#include "Playhead.h"
#include "SDL_Events.h"
GridView::GridView(bool* detached, SDL_FRect* rect, float leftMargin, fract* startTime) : detached(detached), leftMargin(leftMargin), startTime(startTime) {

    if(rect != nullptr && !*detached) {
        dstRect = rect;
    } else {
        dstRect = new SDL_FRect{
            0,
            0,
            800,
            600
        };
    }

    this->gridRect = {
        dstRect->x+leftMargin,
        dstRect->y+topMargin,
        dstRect->w-leftMargin,
        dstRect->h-topMargin
    };


    this->playHead = new Playhead(&gridRect, dstRect, detached, startTime);

    this->width = dstRect->w;
    this->height = dstRect->h;
    this->x = dstRect->x;
    this->y = dstRect->y;

    if(*detached) {
        window = SDL_CreateWindow("Piano Roll", width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);

        SDL_SetWindowParent(window, Home::get()->window);

        renderer = SDL_CreateRenderer(window, NULL);
    } else {
        window = Home::get()->window;
        renderer = Home::get()->renderer;
    }
}

GridView::~GridView() {
    delete playHead;
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
}

bool GridView::tick() {

    customTick();
    if(*detached){
        SDL_RenderPresent(renderer);
    }
    return true;
}


bool GridView::handleInput(SDL_Event& e) {

    toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
    toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
    toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);

    bool running = true;

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
                double gridAtX = (mouseX + scrollX - leftMargin) / cellWidth;
                UpdateGrid();
                scrollX = gridAtX * cellWidth - mouseX + leftMargin;
            } else
                if (isAltPressed) {
                    divHeight *= std::pow(scaleSensitivity, e.wheel.y);
                    if (divHeight < height * 12 / 128 || divHeight <= 0) {
                        divHeight = height * 12 / 128;
                    }

                    double gridAtY = (mouseY + scrollY - topMargin)/ cellHeight;
                    UpdateGrid();
                    scrollY = gridAtY * cellHeight - mouseY + topMargin;
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

        case SDL_EVENT_QUIT:
            running =  false;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            running = false;
        default:
            break;
    }

    handleCustomInput(e);

    if(*detached) {
        if(!running) {
            return false;
        }
    }
    return true;


}

void GridView::moveMouse() {
    SDL_GetMouseState(&mouseX, &mouseY);
    mouseX -= dstRect->x;
    mouseY -= dstRect->y;
}


void GridView::RenderGridTexture() {

    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);

    setRenderColor(renderer, colors.grid);



    for (double x = xOffset + leftMargin; x < width + leftMargin; x += cellWidth) {
        SDL_RenderLine(renderer, x, 0, x, height);
    }

    for (double y = yOffset + topMargin; y < height + topMargin; y += *ySize) {
        SDL_RenderLine(renderer, 0, y, width, y);
    }


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
    return fract(std::floor((mouseX+scrollX-leftMargin)/cellWidth),notesPerBar);
}
