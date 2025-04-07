#include "Home.h"


Home::Home() {
    SDL_Window* window = SDL_CreateWindow("Piano Roll", 1500, 800, 0);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    
    SDL_SetRenderDrawColor(renderer, 100,100,100,255);
    
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
}


Home::~Home() {

}