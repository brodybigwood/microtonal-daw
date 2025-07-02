#include "Home.h"
#include "SDL_Events.h"

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
    
    controls = new ControlArea(controlsHeight, windowWidth, renderer);

    instrumentMenuTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, instMenuWidth, windowHeight-mixerHeight-controlsHeight);

    songRect = SDL_FRect{
        0,
        (float)controlsHeight,
       (float) windowWidth-instMenuWidth,
        (float)windowHeight-controlsHeight-mixerHeight
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
        renderer,
        &songRollDetached
    );

    instrumentMenu = InstrumentMenu::instance();
    instrumentMenu->create(instrumentMenuTexture, renderer, windowWidth-instMenuWidth, controlsHeight, project);

}

bool Home::tick() {


    SDL_Event e;
    while (SDL_PollEvent(&e)) {

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

    //std::cout<<"tickingnging"<<std::endl;
    SDL_SetRenderDrawColor(renderer, 100,100,100,255);
    //std::cout<<"tickingnging"<<std::endl;
    SDL_RenderClear(renderer);
    //std::cout<<"tickingnging"<<std::endl;
    controls->render();
    instrumentMenu->render();

    if(song) {
       song->tick();
    }
    if(pianoRoll) {
        pianoRoll->tick();
    }

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
            instrumentMenu->moveMouse(mouseX,mouseY);
            controls->moveMouse(mouseX, mouseY);
            hoverButtons();
            return true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if(mouseOnEditor()) {
                instrumentMenu->clickMouse(e);
                return true;
                break;
            }

            if(
                mouseY < controlsHeight
            ) {
                controls->handleInput(e);
                return true;
                break;
            }
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

void Home::hoverButtons() {


}

bool Home::mouseOnEditor() {
    if(
        mouseX > windowWidth-instMenuWidth &&
        mouseX < windowWidth &&
        mouseY > controlsHeight &&
        mouseY < windowHeight-mixerHeight
    ) {
        return true;
    } else {
        return false;
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
