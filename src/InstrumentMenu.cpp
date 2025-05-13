#include "InstrumentMenu.h"

InstrumentMenu::InstrumentMenu(SDL_Texture* texture, SDL_Renderer* renderer, int x, int y, Project* project) {
    this->texture = texture;
    this->renderer = renderer;

    this->project = project;


    SDL_GetTextureSize(texture, &width, &height);
    dstRect = {x, y, width, height};
    this->x = x;
    this->y = y;

    TTF_Init();
/*
    auto fontData = BinaryData::Arial_ttf;
    int dataSize = BinaryData::Arial_ttfSize;
    int fontPointSize = 25;
    
    SDL_IOStream* rw = SDL_IOFromMem(const_cast<void*>(static_cast<const void*>(fontData)), dataSize);

    font = TTF_OpenFontIO(rw, 1, fontPointSize);
*/
    font = TTF_OpenFont("assets/fonts/Arial.ttf", 12);

    titleDst = {0, 0 + 25, width, 25};  // x and y = screen position
    outputDst = {0, height-100, width, 25};  // x and y = screen position
   
}


InstrumentMenu::~InstrumentMenu() {
    
}


void InstrumentMenu::renderText() {
 

    titleSurface = TTF_RenderText_Solid(font, name.c_str(), 24, color);
    outputSurface = TTF_RenderText_Solid(font, outputType.c_str(), 24, color);


    
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

textTexture = SDL_CreateTextureFromSurface(renderer, titleSurface);
SDL_RenderTexture(renderer, textTexture, nullptr, &titleDst);
    SDL_DestroyTexture(textTexture);
 textTexture = SDL_CreateTextureFromSurface(renderer, outputSurface);
 SDL_RenderTexture(renderer, textTexture, nullptr, &outputDst);
 SDL_DestroyTexture(textTexture);
  
    SDL_DestroySurface(titleSurface);
    SDL_DestroySurface(outputSurface);

    SDL_SetRenderTarget(renderer, NULL);


}

void InstrumentMenu::render() {
    SDL_SetRenderDrawColor(
        renderer, 
        colors.editorBackground[0],
        colors.editorBackground[1],
        colors.editorBackground[2],
        colors.editorBackground[3]
    );
        
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderFillRect(renderer, &dstRect);

    

    if(project->viewedElement != nullptr) {
        int index = project->viewedElement->index;
        std::string type = project->viewedElement->type;

        if(type == "region") {
            name = project->regions[index].name;
            outputType = project->regions[index].outputType;
        } else if (type == "instrument") {
            name = project->instruments[index].name;
            outputType = project->instruments[index].outputType;
        } else {
            return;     
        }

        renderText();
        SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
  



    }

 

}



void InstrumentMenu::clickMouse(SDL_Event& e) {
    instrument = &(project->instruments[project->viewedElement->index]); 
}

void InstrumentMenu::moveMouse(float x, float y) {
    mouseX = x;
    mouseY = y;
    //getHoveredInstrument();
}


void InstrumentMenu::setInst(Instrument* instrument) {
    this->instrument = instrument;
}