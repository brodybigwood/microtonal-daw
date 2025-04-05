#include <SDL3/SDL.h>
#include <SDL_ttf.h>

#ifndef BUTTON_H
#define BUTTON_H

class Button {

    public:
        Button(int width, int height);
        ~Button();
        
        int width;
        int height;
        void render(SDL_Renderer* renderer); 
        void handleInput(SDL_Event& e);  

        ControlArea* sector;

};

#endif