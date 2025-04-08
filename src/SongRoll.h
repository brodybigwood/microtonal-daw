#include <SDL3/SDL.h>
#include <SDL_ttf.h>

#ifndef SONGROLL_H
#define SONGROLL_H

class SongRoll {

    public:
    SongRoll(int x, int y, int width, int height, SDL_Renderer* renderer);
        ~SongRoll();
        
        SDL_Renderer* renderer;
        int x = 0;
        int y = 0;
        int width;
        int height;

        void render();

        SDL_Texture* texture;

        SDL_FRect dstRect;

};

#endif