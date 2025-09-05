#include "Mixer.h"

Mixer::Mixer() {
    tracks = &(Project::instance()->tracks);
}
Mixer::~Mixer() {}

void Mixer::tick() {
    getHoveredTrack();
    SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    SDL_RenderFillRect(renderer,dstRect);

    SDL_FRect trackRect{
        0,
        dstRect->y,
        trackWidth,
        dstRect->h
    };

    for(auto track : *tracks) {
        gui::mode mode;
        int id = track->id;
        if( id == hoveredId ) {
                mode = gui::mode::hover;
        } else if ( id == selectId ) {
            mode = gui::mode::select;
        } else {
            mode = gui::mode::def;
        }
        track->render(renderer,&trackRect, mode);
        trackRect.x += trackWidth;
    }
}

int Mixer::create() {
    return Project::instance()->createMixerTrack();
}

void Mixer::handleInput(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            moveMouse();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            clickMouse(e);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            scroll += e.wheel.y;
            break;
        default:
            break;
    }
}

void Mixer::moveMouse() {
    SDL_GetMouseState(&mouseX, &mouseY);
}

void Mixer::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            switch(e.button.button) {
                case SDL_BUTTON_LEFT:
                    selectId = hoveredId;
                    break;
                case SDL_BUTTON_RIGHT:
                    break;
                default:
                    break;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            switch(e.button.button) {
                case SDL_BUTTON_LEFT:
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
}

void Mixer::getHoveredTrack() {
    if(mouseY < dstRect->y) {
        hoveredId = -1;
        return;
    }
    size_t i = 0;
    for(auto track : *tracks) {
        int x1 = i * trackWidth;
        int x2 = (i+1) * trackWidth;
        if(mouseX >= x1 && mouseX <= x2) {
            hoveredId = track->id;
            return;
        }
        i++;
    }
    hoveredId = -1;
}
