#include "Home.h"



Home::Home(Project* project, WindowHandler* windowHandler) {
    this->project = project;
    this->windowHandler = windowHandler;
    
    std::cout<<windowWidth<<std::endl;

    window = SDL_CreateWindow("Piano Roll", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(window, NULL);
    
    controls = new ControlArea(controlsHeight, windowWidth, renderer);




    insts = new InstrumentList(controlsHeight, instWidth, windowHeight, renderer);

    song = new SongRoll(
        instWidth, controlsHeight, 
        windowWidth-instWidth, 
        windowHeight-controlsHeight-mixerHeight, 
        renderer, 
        project, 
        windowHandler);
}




Home::~Home() {

}

void Home::tick() {
    SDL_SetRenderDrawColor(renderer, 100,100,100,255);
    
    SDL_RenderClear(renderer);


    insts->render();
    controls->render();
    song->render();

    for(size_t i = 0; i<buttons.size(); i++) {
        Button& button = buttons[i];
        button.render(); 
    }
    
    SDL_RenderPresent(renderer);
}

bool Home::handleInput(SDL_Event& e) {


    switch(e.type) {
        
        case SDL_EVENT_MOUSE_MOTION:
            SDL_GetMouseState(&mouseX, &mouseY);
            song->moveMouse(mouseX-instWidth,mouseY-controlsHeight);
            hoverButtons();
            return true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if(mouseOnSong()) {
            song->clickMouse(e);
            return true;
            break;
        } 
            if(e.button.button == SDL_BUTTON_LEFT) {
                std::cout<<"ss"<<std::endl;
                if(hoveredButton != nullptr) {
                    if(hoveredButton->title == "edit region 1") {
                        windowHandler->createPianoRoll(project->regions[0]);
                        hoveredButton->hovered = false;
                        hoveredButton = nullptr;
                        return true;
                    }
                    if(hoveredButton->title == "edit region 2") {
                        //windowHandler->createPianoRoll(project->regions[1]);
                        project->createRegion(fract(5,4),7);
                        hoveredButton->hovered = false;
                        hoveredButton = nullptr;
                        return true;
                    }
                    return false;
                } else {
                    return false;
                }





                
            }
            default:
            return false;
    }

}

void Home::hoverButtons() {
    for(size_t i = 0; i<buttons.size(); i++) {
        Button& button = buttons[i];
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



bool Home::mouseOnSong() {
    if(
        mouseX > instWidth &&
        mouseX < windowWidth &&
        mouseY > controlsHeight &&
        mouseY < windowHeight-mixerHeight
    ) {
        return true;
    } else {
        return false;
    }
    
}