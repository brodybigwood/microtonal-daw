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

    outRect = new SDL_FRect{
        width / 10.0f,
        height - 20.0f - (0.75f * y),
        8 * width / 10.f,
        20.0f
    };

    rackTitleTextRect = new SDL_FRect{
        width / 10.0f,
        height / 4.0f,
        8 * width / 10.f,
        20.0f
    };

    rackRect = new SDL_FRect{
        x + width / 10.0f,
        y + height / 4.0f,
        8 * width / 10.f,
        (height-y) * 0.75f
    };

    rackTitleRect = new SDL_FRect{
        x + width / 10.0f,
        y + height / 4.0f,
        8 * width / 10.f,
        20.0f
    };

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

    titleSurface = TTF_RenderText_Solid(fonts.mainFont, name.c_str(), name.length(), color);
    outputSurface = TTF_RenderText_Solid(fonts.mainFont, outputType.c_str(), outputType.length(), color);


    
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

textTexture = SDL_CreateTextureFromSurface(renderer, titleSurface);
SDL_RenderTexture(renderer, textTexture, nullptr, rackTitleTextRect);
    SDL_DestroyTexture(textTexture);
 textTexture = SDL_CreateTextureFromSurface(renderer, outputSurface);
 SDL_RenderTexture(renderer, textTexture, nullptr, outRect);
 SDL_DestroyTexture(textTexture);
  
    SDL_DestroySurface(titleSurface);
    SDL_DestroySurface(outputSurface);

    SDL_SetRenderTarget(renderer, NULL);


}



void InstrumentMenu::setViewedElement(std::string type, int index) {
    viewedElement = new element{type, index};
    this->type = type;
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

        if(type == "region") {
            name = project->regions[index]->name;
            outputType = "region outputs";
        } else if (type == "instrument") {
            name = project->instruments[index]->name;
            outputType = "instrument outputs";


        } else {
            return;     
        }


        SDL_SetRenderDrawColor(renderer,50,50,50,255);
        SDL_RenderFillRect(renderer, rackRect);

        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderRect(renderer, rackRect);

        SDL_SetRenderDrawColor(renderer,75,75,75,255);
        SDL_RenderFillRect(renderer, rackTitleRect);

        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderRect(renderer, rackTitleRect);

        for(auto plugin : plugins) {
            plugin.win->render();
            plugin.proc->render();
        }

        renderText();
        SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
  



    }

 

}

bool inside(float px, float py, float x, float y, float w, float h) {
    return px > x && px < x + w && py > y && py < y + h;
}



void InstrumentMenu::clickMouse(SDL_Event& e) {
    for (auto plugin : plugins) {
        if(plugin.win->hover()) {
            plugin.win->onClick();
        }
        if(plugin.proc->hover()) {
            plugin.proc->onClick();
        }
    }
}

void InstrumentMenu::moveMouse(float x, float y) {
    mouseX = x;
    mouseY = y;
    hover();
}

void InstrumentMenu::hover() {

}


void InstrumentMenu::setInst(Instrument* instrument) {
    this->instrument = instrument;

    pluginHeight = rackRect->h / 10.0f;
    float pluginY = rackRect->y + rackTitleRect->h;

    for (auto plug : plugins) {
        delete plug.win;
        delete plug.proc;
    }
    plugins.clear();

    std::cout << "setting inst" <<std::endl;
    int i = 0;

    for(auto& plugin : instrument->rack.plugins) {

        plugItem item{};

        Button* win = new Button(
            renderer
        );

        win->dstRect = new SDL_FRect{
            rackRect->x + plugMarginX,
            pluginY + plugMarginY,
            rackRect->w  - (3 * plugMarginX),
            pluginHeight - plugMarginY
        };

        win->onClick = [this,i] {
            this->instrument->rack.plugins[i]->showWindow();
        };

        win->hover = [this, win] {
            if (inside(this->mouseX, this->mouseY, win->dstRect->x, win->dstRect->y, win->dstRect->w, win->dstRect->h)) {
                return true;
            } else {
                return false;
            }
        };

        win->activated = [this,i] {
            return this->instrument->rack.plugins[i]->windowOpen;
        };

        item.win = win;

        Button* proc = new Button(
            renderer
        );

        proc->dstRect = new SDL_FRect{
            rackRect->x + rackRect->w  - (2 * plugMarginX),
            pluginY + plugMarginY,
            plugMarginX,
            pluginHeight - plugMarginY
        };

        proc->onClick = [this,i] {
            this->instrument->rack.plugins[i]->toggle();
        };

        proc->hover = [this, proc] {
            if (inside(this->mouseX, this->mouseY, proc->dstRect->x, proc->dstRect->y, proc->dstRect->w, proc->dstRect->h)) {
                return true;
            } else {
                return false;
            }
        };

        proc->activated = [this,i] {
            return this->instrument->rack.plugins[i]->processing;
        };

        item.proc = proc;

        plugins.push_back(item);

        pluginY += pluginHeight;

        i++;
    }



}
