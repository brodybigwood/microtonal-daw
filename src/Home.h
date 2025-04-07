
#include <SDL_ttf.h>
#include "Region.h"
#include "Note.h"

#ifndef HOME_H
#define HOME_H

class Home {

    public:

        Home();
        ~Home();

        SDL_Window* window;
        SDL_Renderer* renderer;
};

#endif