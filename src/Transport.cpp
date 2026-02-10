#include "Transport.h"
#include "GridView.h"
#include "Project.h"
#include "Button.h"
#include <functional>

Transport::Transport(GridView* view) : view(view), renderer(view->renderer), dstRect(view->dstRect) {

    togglePlay = new Button(renderer);
    togglePlay->activated = [] {
        return Project::instance()->isPlaying;
    };
    togglePlay->dstRect = new SDL_FRect {
    view->dstRect->x + 50,view->dstRect->y + 15,25,25
    };
    togglePlay->hover = [this] {
        return (
            this->mouseX >= togglePlay->dstRect->x && this->mouseX <= togglePlay->dstRect->x + togglePlay->dstRect->w &&
            this->mouseY >= togglePlay->dstRect->y && this->mouseY <= togglePlay->dstRect->y + togglePlay->dstRect->h
        );
    };
    togglePlay->onClick = [] {
        Project::instance()->togglePlaying();
    };



    stop = new Button(renderer);
    stop->activated = [] {
        return !Project::instance()->processing;
    };
    stop->dstRect = new SDL_FRect {
        view->dstRect->x + 100 ,view->dstRect->y + 15,25,25
    };
    stop->hover = [this] {
        return (
            this->mouseX >= stop->dstRect->x && this->mouseX <= stop->dstRect->x + stop->dstRect->w &&
            this->mouseY >= stop->dstRect->y && this->mouseY <= stop->dstRect->y + stop->dstRect->h
        );
    };
    stop->onClick = [] {
        Project::instance()->stop();
    };



}

void Transport::moveMouse(float mouseX, float mouseY) {
    this->mouseX = mouseX;
    this->mouseY = mouseY;
}

void Transport::clickMouse() {
    if(togglePlay->hover()) {
        togglePlay->onClick();
    }
    if(stop->hover()) {
        stop->onClick();
    }
}

Transport::~Transport() {
    delete togglePlay->dstRect;
    delete stop->dstRect;

    delete togglePlay;
    delete stop;
}

void Transport::render() {
    uint8_t color[4] = {50, 50, 50, 255};
    view->setRenderColor(color);
    SDL_FRect rect{0,0,view->dstRect->w, view->topMargin};
    SDL_RenderFillRect(view->renderer, &rect);

    togglePlay->render();
    stop->render();
}
