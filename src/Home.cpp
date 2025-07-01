#include "Home.h"

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

    insts = new InstrumentList(controlsHeight, instWidth, windowHeight-mixerHeight, renderer, project);

    songRect = new SDL_FRect{
        0,
        controlsHeight,
        windowWidth-instWidth-instMenuWidth,
        windowHeight-controlsHeight-mixerHeight
    };
}

Home::~Home() {

}

void Home::createRoll(bool detached) {
    song = new SongRoll(
        songRect,
        instWidth,
        renderer,
        project,
        windowHandler);

    instrumentMenu = InstrumentMenu::instance();
    instrumentMenu->create(instrumentMenuTexture, renderer, windowWidth-instMenuWidth, controlsHeight, project);
}

void Home::tick() {
    //std::cout<<"tickingnging"<<std::endl;
    SDL_SetRenderDrawColor(renderer, 100,100,100,255);
    //std::cout<<"tickingnging"<<std::endl;
    SDL_RenderClear(renderer);
    //std::cout<<"tickingnging"<<std::endl;
    insts->render();
    controls->render();
    song->render();
    instrumentMenu->render();

}

bool Home::sendInput(SDL_Event& e) {
    if(pianoRoll != nullptr && mouseOn(&pianoRollRect)) {
        pianoRoll->handleInput(e);
        return true;
    }
    if(song != nullptr && mouseOn(songRect)) {
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
            insts->moveMouse(mouseX,mouseY-controlsHeight);
            instrumentMenu->moveMouse(mouseX,mouseY);
            controls->moveMouse(mouseX, mouseY);
            hoverButtons();
            return true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if(mouseOnInst()) {
                insts->clickMouse(e);
                return true;
                break;
            }
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
            return false;


        case SDL_EVENT_KEY_DOWN:
            switch (e.key.scancode) {
                case SDL_SCANCODE_SPACE:
                    project->togglePlaying();
                    return true;
                    break;
                default:
                    return false;
            }
            break;


        default:
            return false;
    }

}

void Home::hoverButtons() {


}


bool Home::mouseOnInst() {
    if(
        mouseX > 0 &&
        mouseX < instWidth &&
        mouseY > controlsHeight &&
        mouseY < windowHeight-mixerHeight
    ) {
        return true;
    } else {
        return false;
    }
    
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
