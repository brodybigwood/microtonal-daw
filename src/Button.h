#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include "ControlArea.h"
#include <string>


#ifndef BUTTON_H
#define BUTTON_H

class Button {

    public:
        Button(std::string title, float x, float y, int width, int height, SDL_Renderer* renderer = nullptr);
        ~Button();
        
        SDL_Renderer* renderer;


        float x;
        float y;

        float width = 50;
        float height = 50;

        void render(); 
        void handleInput(SDL_Event& e);  

        SDL_Texture* texture;
        ControlArea* sector;

        SDL_FRect dstRect;

        void hover();

        SDL_Color color = {255, 255, 255, 255};

        bool hovered = false;

        std::string title;


};

#endif