#include "ControlArea.h"
#include "EventManager.h"
#include "Button.h"
#include <SDL3/SDL_oldnames.h>
#include "styles.h"

ControlArea::ControlArea(int height, int width, SDL_Renderer* renderer) {

    this->renderer = renderer;

    this->height = height;
    this->width = width;

    dstRect = {x, y, width, height};

    Button play("play", 200 + 50.0f/5, 50.0f/5, 50.0f/3, 50.0f/3, renderer);
    Button stop("stop", 200 + 6*50.0f/5, 50.0f/5, 50.0f/3, 50.0f/3, renderer);

    buttons.push_back(play);
    buttons.push_back(stop);

    this->project = Project::instance();

}
ControlArea::~ControlArea() {

}


void ControlArea::render() {

    SDL_SetRenderDrawColor(renderer, colors.controlsBackground.r, colors.controlsBackground.g, colors.controlsBackground.b, colors.controlsBackground.a);
    SDL_RenderFillRect(renderer, &dstRect);


    for(auto& button : buttons) {
        button.render();
    }

}

void ControlArea::hoverButtons() {
    for(Button& button : buttons) {
        if(
            mouseX >= button.x &&
            mouseX <= button.x+button.width &&
            mouseY >= button.y &&
            mouseY <= button.y+button.height
        ) {
            hoveredButton = &button;
            hoveredButton->hovered = true;
            break;
        } else {
            button.hovered = false;
            hoveredButton = nullptr;
        }
    }

}

void ControlArea::moveMouse(float mouseX, float mouseY) {
    this->mouseX = mouseX;
    this->mouseY = mouseY;

    hoverButtons();
}


void ControlArea::handleInput(SDL_Event& e) {
    if(e.button.button == SDL_BUTTON_LEFT) {
        if(hoveredButton != nullptr) {
            if(hoveredButton->title == "play") {
                project->play();
                hoveredButton->hovered = false;
                hoveredButton = nullptr;
                return;
            }
            if(hoveredButton->title == "stop") {
                project->stop();
                hoveredButton->hovered = false;
                hoveredButton = nullptr;
                return;
            }
            return;
        } else {
            return;
        }
    }
}
