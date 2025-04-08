#include "Home.h"



Home::Home(WindowHandler* windowHandler) {

    this->windowHandler = windowHandler;
    window = SDL_CreateWindow("Piano Roll", 1500, 800, 0);

    renderer = SDL_CreateRenderer(window, NULL);
    
    SDL_SetRenderDrawColor(renderer, 100,100,100,255);
    
    SDL_RenderClear(renderer);


    Button playButton("edit region 1", 50,50, 50,50,renderer);

    Button playButton2("edit region 2", 150,50, 50,50,renderer);

    buttons.push_back(playButton);
    buttons.push_back(playButton2);


}




Home::~Home() {

}

void Home::tick() {
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
            hoverButtons();
            return true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        std::cout<<"ss"<<std::endl;
            if(e.button.button == SDL_BUTTON_LEFT) {
                std::cout<<"ss"<<std::endl;
                if(hoveredButton != nullptr) {
                    if(hoveredButton->title == "edit region 1") {
                        windowHandler->createPianoRoll(midiRegion1);
                        hoveredButton = nullptr;
                        return true;
                    }
                    if(hoveredButton->title == "edit region 2") {
                        windowHandler->createPianoRoll(midiRegion2);
                        hoveredButton = nullptr;
                        return true;
                    }
                    return false;
                } else {
                    return false;
                }



        default:
        return false;

                
            }
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