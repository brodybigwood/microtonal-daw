#include "GridView.h"
#include "Playhead.h"
#include "SDL_Events.h"
#include "Transport.h"
GridView::GridView(bool* detached, SDL_FRect* rect, float leftMargin) : detached(detached), leftMargin(leftMargin) {

    if(rect != nullptr && !*detached) {
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

    if(*detached) {
        window = SDL_CreateWindow("Piano Roll", width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);

        SDL_SetWindowParent(window, Home::get()->window);

        renderer = SDL_CreateRenderer(window, NULL);
    } else {
        window = Home::get()->window;
        renderer = Home::get()->renderer;
    }


    this->playHead = new Playhead(&gridRect, dstRect, detached, startTime);
    this->transport = new Transport(this);
}

void GridView::createGridRect() {
    gridRect = {
        dstRect->x+leftMargin,
        dstRect->y+topMargin,
        dstRect->w-leftMargin-rightMargin,
        dstRect->h-topMargin
    };
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
            transport->clickMouse();
            clickMouse(e);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
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

    transport->moveMouse(mouseX, mouseY);
}


void GridView::RenderGridTexture() {

    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);

    setRenderColor(colors.grid);

    for(auto line : times) {
        float val = getX(line);
        SDL_RenderLine(renderer, val, 0, val, height);
    }

    for(auto line : lines) {
        float val = getY(line);
        SDL_RenderLine(renderer, 0, val, width, val);
    }
}

void GridView::setRenderColor(uint8_t code[4]) {
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

fract GridView::getHoveredTime() {
    return fract(std::floor((mouseX+scrollX-leftMargin)/dW),1);
}

float GridView::getX(float time) {
    return time * dW + leftMargin - scrollX;
}
