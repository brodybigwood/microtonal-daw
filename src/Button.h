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

        void setColor(SDL_Color& color);

        SDL_Texture* texture;
        ControlArea* sector;

        SDL_FRect dstRect;

        void hover();

        bool hovered = false;

        void toggleState();
        bool activated = false;

        SDL_Color inactive = {30, 30, 30, 255};
        SDL_Color inactive_hover = {40, 40, 40, 255};

        SDL_Color active = {160, 80, 60, 255};
        SDL_Color active_hover = {160, 120, 140, 255};

        SDL_Color border = {0,0,0,255};

        std::string title;


};

#endif
