#include "ControlArea.h"
#include "EventManager.h"
#include "Button.h"
#include <SDL3/SDL_oldnames.h>
#include "styles.h"

ControlArea::ControlArea(int height, int width, SDL_Renderer* renderer) {

    this->renderer = renderer;
    this->project = Project::instance();

    this->height = height;
    this->width = width;

    dstRect = {x, y, width, height};

    Button* play = new Button("play", 200 + 50.0f/5, 50.0f/5, 50.0f/3, 50.0f/3, renderer);
    Button* stop = new Button("stop", 200 + 6*50.0f/5, 50.0f/5, 50.0f/3, 50.0f/3, renderer);

    play->onClick = [this] {
        this->project->togglePlaying();
    };
    stop->onClick = [this] {
        this->project->stop();
    };

    play->activated = [this] {
        return this->project->isPlaying;
    };
    stop->activated = [this] {
        return !this->project->processing;
    };

    buttons.push_back(play);
    buttons.push_back(stop);

    for (Button* btn : buttons) {

        btn->hover = [this, btn] {
            return (
                this->mouseX >= btn->x && this->mouseX <= btn->x + btn->width &&
                this->mouseY >= btn->y && this->mouseY <= btn->y + btn->height
            );
        };
    }

}
ControlArea::~ControlArea() {

}


void ControlArea::render() {

    SDL_SetRenderDrawColor(renderer, colors.controlsBackground.r, colors.controlsBackground.g, colors.controlsBackground.b, colors.controlsBackground.a);
    SDL_RenderFillRect(renderer, &dstRect);

    for(auto button : buttons) {
        button->render();
    }

}

void ControlArea::moveMouse(float mouseX, float mouseY) {
    this->mouseX = mouseX;
    this->mouseY = mouseY;
}


void ControlArea::handleInput(SDL_Event& e) {
    if (e.button.button == SDL_BUTTON_LEFT) {
        for (auto* btn : buttons) {
            if (btn->hover && btn->hover()) {
                if (btn->onClick) btn->onClick();
                break;
            }
        }
    }
}
