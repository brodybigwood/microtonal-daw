#include <SDL3/SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <functional>


#ifndef BUTTON_H
#define BUTTON_H

class Button {

    public:
        Button(SDL_Renderer* renderer = nullptr);
        ~Button();
        
        SDL_Renderer* renderer;

        std::function<bool()> activated;

        std::function<bool()> hover;
        std::function<void()> onClick;

        void render(); 
        void handleInput(SDL_Event& e);  

        void setColor(SDL_Color& color);

        SDL_Texture* texture;

        SDL_FRect* dstRect;

        SDL_Color inactive = {30, 30, 30, 255};
        SDL_Color inactive_hover = {40, 40, 40, 255};

        SDL_Color active = {160, 80, 60, 255};
        SDL_Color active_hover = {160, 120, 140, 255};

        SDL_Color border = {0,0,0,255};

        std::string title;


};

#endif
