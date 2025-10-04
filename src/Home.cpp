#include "Home.h"
#include "SDL_Events.h"
#include "NodeEditor.h"

Home* Home::get() {
    static Home h(Project::instance());
    return &h;
}

Home::Home(Project* project) {
    this->project = project;

    WindowHandler* windowHandler = WindowHandler::instance();

    this->window = windowHandler->mainWindow;
    this->windowWidth = windowHandler->windowWidth;
    this->windowHeight = windowHandler->windowHeight;


    renderer = windowHandler->renderer;

    songRect = SDL_FRect{
        0,
        0,
       (float) windowWidth,
        (float)windowHeight
    };


}

Home::~Home() {

}

void Home::createRoll() {
    if(song) {
        return;
    }
    song = new SongRoll(
        &songRect,
        &songRollDetached
    );

}

bool Home::tick() {


    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        
        if(e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_SPACE) {
            project->togglePlaying();
            return true;
        }

        uint32_t id = getEventWindowID(e);


        if (id == SDL_GetWindowID(window)) {
            if(!handleInput(e)) {
                return false;
            }
        }

        if(pianoRoll != nullptr && pianoRollDetached) {
            if (id == SDL_GetWindowID(pianoRoll->window)) {
                if(!pianoRoll->handleInput(e)) {
                    delete pianoRoll;
                    pianoRoll = nullptr;
                }
            }
        }

        if(song != nullptr && songRollDetached) {
            if (id == SDL_GetWindowID(song->window)) {
                if(!song->handleInput(e)) {
                    delete song;
                    song = nullptr;
                }
            }
        }
    }

    SDL_SetRenderDrawColor(renderer, 100,100,100,255);
    SDL_RenderClear(renderer);

    if(song) {
       song->tick();
    }
    if(pianoRoll) {
        pianoRoll->tick();
    }
    NodeEditor::get()->tick();

    return true;
}

bool Home::sendInput(SDL_Event& e) {
    if (pianoRoll && !pianoRollDetached && mouseOn(&pianoRollRect)) {
        pianoRoll->handleInput(e);
        return true;
    }
    if (song && !songRollDetached && mouseOn(&songRect)) {
        song->handleInput(e);
        return true;
    }
    return false;
}


bool Home::handleInput(SDL_Event& e) {

    const auto* windowHandler = WindowHandler::instance();

    if(e.type == SDL_EVENT_MOUSE_MOTION) {
        SDL_GetMouseState(&mouseX, &mouseY);
    }

    if(sendInput(e)) {
        return true;
    }
    switch(e.type) {
        
        case SDL_EVENT_MOUSE_MOTION:
            return true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            return true;


        case SDL_EVENT_KEY_DOWN:
            switch (e.key.scancode) {
                case SDL_SCANCODE_SPACE:
                    project->togglePlaying();
                    return true;
                    break;
                default:
                    return true;
            }
            break;

        case SDL_EVENT_QUIT:
            return false;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return false;


        default:
            return true;
    }

}

bool Home::mouseOn(SDL_FRect* rect) {
    if(
        mouseX > rect->x &&
        mouseX < rect->x + rect->w &&
        mouseY > rect->y &&
        mouseY < rect->y + rect->h
    ) {
        return true;
    } else {
        return false;
    }
}
