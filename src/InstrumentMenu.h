#include "Project.h"
#include "Instrument.h"
#include <SDL3/SDL.h>
#include <JuceHeader.h>
#include <iostream>
#include <vector>
#include "BinaryData.h"
#include "styles.h"


#ifndef INSTRUMENTMENU_H
#define INSTRUMENTMENU_H

class InstrumentMenu {
    public:
        InstrumentMenu(SDL_Texture* texture, SDL_Renderer* renderer, int x, int y, Project* project);
        ~InstrumentMenu();

        Project* project;

        tracktion_engine::Edit* edit;

        SDL_Texture* texture;
        SDL_Renderer* renderer;

        std::string name;
        std::string outputType;
        std::vector<int> outputs;

        float width, height;

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

        SDL_FRect titleDst;
        SDL_FRect outputDst;

        SDL_Color color = {255, 255, 255};  // white

        TTF_Font* font;
        
SDL_Surface* titleSurface;

SDL_Surface* outputSurface;
};

#endif