#include "Project.h"
#include "Instrument.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <vector>
#include "styles.h"
#include "Button.h"


#ifndef INSTRUMENTMENU_H
#define INSTRUMENTMENU_H

class InstrumentMenu {
    public:
        InstrumentMenu();

        void create(SDL_Texture* texture, SDL_Renderer* renderer, int x, int y, Project* project);

        ~InstrumentMenu();

        static InstrumentMenu* instance();

        Project* project;

        void hover();

        std::string type = "instrument";

        SDL_Texture* texture;
        SDL_Renderer* renderer;

        std::string name;
        std::string outputType;
        std::vector<int> outputs;

        struct plugItem{
            Button* win;
            Button* proc;
        };

        std::vector<plugItem> gPlugs;
        std::vector<plugItem> ePlugs;

        std::unique_ptr<Button> addInst;

        float plugMarginX = 20.0f;
        float plugMarginY = 20.0f;

        SDL_FRect* rackRect;
        SDL_FRect* rackTitleRect;

        SDL_FRect* rackTitleTextRect;
        SDL_FRect* outRect;

        float pluginHeight;

        float width, height;

        struct element {
            std::string type;
            int index;
        };

        element* viewedElement = nullptr;

        void setViewedElement(std::string type, int index);

        void render();

        void renderText();

        SDL_FRect dstRect;

        void clickMouse(SDL_Event& e);

        void moveMouse(float, float);

        float mouseX;
        float mouseY;

        bool lmb, rmb;

        int x, y;

        float textW, textH;

        SDL_Texture* textTexture;

        SDL_Color color = {255, 255, 255, 255};  // white

        TTF_Font* font;
        
SDL_Surface* titleSurface;

SDL_Surface* outputSurface;

Instrument* instrument;

Rack* generators;
Rack* effects;

void setInst(Instrument* instrument);
};

#endif
