#include "InstrumentMenu.h"
#include <SDL3/SDL_oldnames.h>

InstrumentMenu::InstrumentMenu() {}

void InstrumentMenu::create(SDL_Texture* texture, SDL_Renderer* renderer, int x, int y, Project* project) {
    this->texture = texture;
    this->renderer = renderer;

    this->project = project;


    SDL_GetTextureSize(texture, &width, &height);
    dstRect = {x, y, width, height};
    this->x = x;
    this->y = y;

    TTF_Init();

    titleDst = {0, 0 + 25, width, 25};  // x and y = screen position
    outputDst = {0, height-100, width, 25};  // x and y = screen position

    rackRect = new SDL_FRect{ x + width / 4.0f, y + height / 4.0f, width / 2.0f, (height-y) * 0.75f };

}

InstrumentMenu* InstrumentMenu::instance() {
    static InstrumentMenu menu;
    return &menu;
}


InstrumentMenu::~InstrumentMenu() {
    
}


void InstrumentMenu::renderText() {
    if (fonts.mainFont) {

    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: mainFont is NULL in PianoRoll::RenderKeys!\n");
        // Handle the error (e.g., return, don't render)
    }

    titleSurface = TTF_RenderText_Solid(fonts.mainFont, name.c_str(), 24, color);
    outputSurface = TTF_RenderText_Solid(fonts.mainFont, outputType.c_str(), 24, color);


    
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



void InstrumentMenu::setViewedElement(std::string type, int index) {
    viewedElement = new element{type, index};

    if(type == "instrument") {
        setInst(project->instruments[index]);
    }
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

    

    if(viewedElement != nullptr) {
        int index = viewedElement->index;
        std::string type = viewedElement->type;

        if(type == "region") {
            name = project->regions[index]->name;
            outputType = project->regions[index]->outputType;
        } else if (type == "instrument") {
            name = project->instruments[index]->name;
            outputType = project->instruments[index]->outputType;

            SDL_SetRenderDrawColor(renderer,50,50,50,255);
            SDL_RenderFillRect(renderer, rackRect);





        } else {
            return;     
        }

        for(auto button : buttons) {
            button->render();
        }

        renderText();
        SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
  



    }

 

}

bool inside(float px, float py, float x, float y, float w, float h) {
    return px > x && px < x + w && py > y && py < y + h;
}



void InstrumentMenu::clickMouse(SDL_Event& e) {
    for (size_t i = 0; i < buttons.size(); ++i) {
        auto& btn = buttons[i];
        if (inside(mouseX, mouseY, btn->x, btn->y, btn->width, btn->height)) {
            instrument->rack.plugins[i]->showWindow();
            return;
        }
    }

}

void InstrumentMenu::moveMouse(float x, float y) {
    mouseX = x;
    mouseY = y;
    hover();
}

void InstrumentMenu::hover() {
    for(Button* btn : buttons) {
        if(
            mouseX > btn->x &&
            mouseX < btn->x + btn->width &&
            mouseY > btn->y &&
            mouseY < btn->y + btn->height
        ) {
            btn->hovered = true;
            return;
        } else {
            btn->hovered = false;
        }
    }
}


void InstrumentMenu::setInst(Instrument* instrument) {
    this->instrument = instrument;

    pluginHeight = (rackRect->h - plugMarginY)  / instrument->rack.plugins.size();
    float pluginY = rackRect->y;

    std::cout << "setting inst" <<std::endl;
    for(auto& plugin : instrument->rack.plugins) {

        Button* btn = new Button(plugin->name, rackRect->x + plugMarginX, pluginY + plugMarginY, rackRect->w  - (2 * plugMarginX), pluginHeight - plugMarginY, renderer);


        btn->color = {80,90,80,255};

        buttons.push_back(btn);

        std::cout << "created btn" <<std::endl;
        pluginY += pluginHeight;
    }



}
