#include "TextInput.h"
#include <iostream>


TextInput* TextInput::get() {
    static TextInput t;
    return &t;
}

void TextInput::tick(SDL_Event& e) {

    if(e.type == SDL_EVENT_TEXT_INPUT) {
        text += e.text.text;
    }
        std::cout<<text<<std::endl;


    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.key == SDLK_BACKSPACE && !text.empty()) {
            text.pop_back();
        }

    }

    if ( (
        e.type == SDL_EVENT_TEXT_INPUT && e.text.text[0] == '\n'
        ) || (
        e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN
        ) 
        ) {
        std::cout<<"RETURN"<<std::endl;
        enter();
        active = false;
        text = "";
        return;
    }


    SDL_SetRenderDrawColor(renderer, 200,200,200,255);
    SDL_RenderFillRect(renderer, &dstRect);

}
