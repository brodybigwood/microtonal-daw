#include "Home.h"



Home::Home(Project* project, WindowHandler* windowHandler) {
    this->project = project;
    this->windowHandler = windowHandler;

    this->window = windowHandler->mainWindow;
    this->windowWidth = windowHandler->windowWidth;
    this->windowHeight = windowHandler->windowHeight;
    
    std::cout<<windowWidth<<std::endl;


    renderer = SDL_CreateRenderer(window, NULL);
    
    controls = new ControlArea(controlsHeight, windowWidth, renderer);


    Button play("play", instWidth + controlsHeight/5, controlsHeight/5, controlsHeight/3, controlsHeight/3, renderer);
    Button stop("stop", instWidth + 6*controlsHeight/5, controlsHeight/5, controlsHeight/3, controlsHeight/3, renderer);

    buttons.push_back(play);
    buttons.push_back(stop);

    insts = new InstrumentList(controlsHeight, instWidth, windowHeight-mixerHeight, renderer, project);

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
    //std::cout<<"tickingnging"<<std::endl;
    SDL_SetRenderDrawColor(renderer, 100,100,100,255);
    //std::cout<<"tickingnging"<<std::endl;
    SDL_RenderClear(renderer);
    //std::cout<<"tickingnging"<<std::endl;

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
            insts->moveMouse(mouseX,mouseY-controlsHeight);
            hoverButtons();
            return true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if(mouseOnSong()) {
            song->clickMouse(e);
            return true;
            break;
        } 
        if(mouseOnInst()) {
            insts->clickMouse(e);
            return true;
            break;
        } 
            if(e.button.button == SDL_BUTTON_LEFT) {
                std::cout<<"ss"<<std::endl;
                if(hoveredButton != nullptr) {
                    if(hoveredButton->title == "play") {
                        project->play();
                        hoveredButton->hovered = false;
                        hoveredButton = nullptr;
                        return true;
                    }
                    if(hoveredButton->title == "stop") {
                        //windowHandler->createPianoRoll(project->regions[1]);
                        project->stop();
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