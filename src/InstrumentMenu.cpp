#include "InstrumentMenu.h"
#include <SDL3/SDL_oldnames.h>
#include <memory>
#include "Region.h"
#include "Rack.h"
InstrumentMenu::InstrumentMenu() {}

void InstrumentMenu::create(SDL_Texture* texture, SDL_Renderer* renderer, int x, int y, Project* project) {
    this->texture = texture;
    this->renderer = renderer;

    this->project = project;


    SDL_GetTextureSize(texture, &width, &height);
    dstRect = {x, y, width, height};
    this->x = x;
    this->y = y;

    rackTitleTextRect = new SDL_FRect{
        width / 10.0f,
        height / 10.0f,
        8 * width / 10.f,
        20.0f
    };

    gRect = new SDL_FRect{
        x + width / 20.0f,
        y + height / 8.0f,
        8 * width / 20.f,
        (height-y) * 0.5f
    };

    eRect = new SDL_FRect{
        x + (3 * width / 20.0f) + (8 * width / 20.f),
        y + height / 8.0f,
        8 * width / 20.f,
        (height-y) * 0.5f
    };

    rackTitleRect = new SDL_FRect{
        x + width / 10.0f,
        y + height / 10.0f,
        8 * width / 10.f,
        20.0f
    };

    addInst = std::make_unique<Button>(renderer);
    addInst->dstRect = new SDL_FRect{
        x + width / 20.0f,
        eRect->h + eRect->y + 10,
        8 * width / 20.0f,
        20.0f
    };

    addFX = std::make_unique<Button>(renderer);
    addFX->dstRect = new SDL_FRect{
        x + (19 * width / 20.0f) - (8 * width / 20.f),
        eRect->h + eRect->y + 10,
        8 * width / 20.f,
        20.0f
    };

    outRect = new SDL_FRect{
        width / 10.0f,
        addFX->dstRect->y + addFX->dstRect->h + 10,
        8 * width / 10.f,
        20.0f
    };



    addInst->activated = [] {
        return false;
    };

    addInst->hover = [this, &rect = addInst->dstRect] {
        return rect && mouseX >= rect->x && mouseX <= rect->x + rect->w && mouseY >= rect->y && mouseY <= rect->y + rect->h;
    };

    addInst->onClick = [this] {
        this->generators->addPlugin("/home/brody/Downloads/surge-xt-linux-x86_64-1.3.4/lib/vst3/Surge XT.vst3/Contents/x86_64-linux/Surge XT.so");
        //    rack.addPlugin("/usr/lib/vst3/Vital.vst3/Contents/x86_64-linux/Vital.so");

        this->setInst(this->instrument);
    };

    addFX->activated = [] {
        return false;
    };

    addFX->hover = [this, &rect = addFX->dstRect] {
        return rect && mouseX >= rect->x && mouseX <= rect->x + rect->w && mouseY >= rect->y && mouseY <= rect->y + rect->h;
    };

    addFX->onClick = [this] {
        this->effects->addPlugin("/mnt/2TB/home/brody/Downloads/FirComp_linuxVST3/FirComp.vst3/Contents/x86_64-linux/FirComp.so");
        this->setInst(this->instrument);
    };

}

InstrumentMenu* InstrumentMenu::instance() {
    static InstrumentMenu menu;
    return &menu;
}


InstrumentMenu::~InstrumentMenu() {
    for (auto& plug : gPlugs) {
        delete plug.win;
        delete plug.proc;
    }

    for (auto& plug : ePlugs) {
        delete plug.win;
        delete plug.proc;
    }

    delete gRect;
    delete eRect;
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
    delete viewedElement;
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
        SDL_RenderFillRect(renderer, gRect);

        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderRect(renderer, gRect);

        SDL_SetRenderDrawColor(renderer,50,50,50,255);
        SDL_RenderFillRect(renderer, eRect);

        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderRect(renderer, eRect);

        SDL_SetRenderDrawColor(renderer,75,75,75,255);
        SDL_RenderFillRect(renderer, rackTitleRect);

        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderRect(renderer, rackTitleRect);

        for(auto plugin : gPlugs) {
            plugin.win->render();
            plugin.proc->render();
        }
        for(auto plugin : ePlugs) {
            plugin.win->render();
            plugin.proc->render();
        }

        renderText();
        SDL_RenderTexture(renderer, texture, nullptr, &dstRect);
  
        addInst->render();
        addFX->render();


    }

 

}

bool inside(float px, float py, float x, float y, float w, float h) {
    return px > x && px < x + w && py > y && py < y + h;
}



void InstrumentMenu::clickMouse(SDL_Event& e) {
    for (auto plugin : gPlugs) {
        if(plugin.win->hover()) {
            plugin.win->onClick();
        }
        if(plugin.proc->hover()) {
            plugin.proc->onClick();
        }
    }

    for (auto plugin : ePlugs) {
        if(plugin.win->hover()) {
            plugin.win->onClick();
        }
        if(plugin.proc->hover()) {
            plugin.proc->onClick();
        }
    }

    if(addInst->hover()){
        addInst->onClick();
    }
    if(addFX->hover()){
        addFX->onClick();
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
    this->generators = &(instrument->generators);
    this->effects = &(instrument->effects);

    pluginHeight = gRect->h / 20.0f;
    float pluginY = gRect->y;

    for (auto& plug : gPlugs) {
        delete plug.win;
        delete plug.proc;
    }
    gPlugs.clear();

    int i = 0;

    for(auto& plugin : instrument->generators.plugins) {

        plugItem item{};

        Button* win = new Button(
            renderer
        );

        win->dstRect = new SDL_FRect{
            gRect->x + plugMarginX,
            pluginY + plugMarginY,
            gRect->w  - (3 * plugMarginX),
            pluginHeight - plugMarginY
        };

        win->onClick = [this,i] {
            this->instrument->generators.plugins[i]->showWindow();
        };

        win->hover = [this, win] {
            if (inside(this->mouseX, this->mouseY, win->dstRect->x, win->dstRect->y, win->dstRect->w, win->dstRect->h)) {
                return true;
            } else {
                return false;
            }
        };

        win->activated = [this,i] {
            return this->instrument->generators.plugins[i]->windowOpen;
        };

        win->inactive = SDL_Color{50,30,30,255};

        item.win = win;

        Button* proc = new Button(
            renderer
        );

        proc->dstRect = new SDL_FRect{
            gRect->x + gRect->w  - (5 * plugMarginX),
            pluginY + plugMarginY,
            plugMarginX * 4,
            pluginHeight - plugMarginY
        };

        proc->onClick = [this,i] {
            this->instrument->generators.plugins[i]->toggle();
        };

        proc->hover = [this, proc] {
            if (inside(this->mouseX, this->mouseY, proc->dstRect->x, proc->dstRect->y, proc->dstRect->w, proc->dstRect->h)) {
                return true;
            } else {
                return false;
            }
        };

        proc->activated = [this,i] {
            return this->instrument->generators.plugins[i]->processing;
        };

        item.proc = proc;

        gPlugs.push_back(item);

        pluginY += pluginHeight;

        i++;
    }

    pluginY = eRect->y;

    for (auto& plug : ePlugs) {
        delete plug.win;
        delete plug.proc;
    }
    ePlugs.clear();

    i = 0;

    for(auto& plugin : instrument->effects.plugins) {

        plugItem item{};

        Button* win = new Button(
            renderer
        );

        win->dstRect = new SDL_FRect{
            eRect->x + plugMarginX,
            pluginY + plugMarginY,
            eRect->w  - (3 * plugMarginX),
            pluginHeight - plugMarginY
        };

        win->onClick = [this,i] {
            this->instrument->effects.plugins[i]->showWindow();
        };

        win->hover = [this, win] {
            if (inside(this->mouseX, this->mouseY, win->dstRect->x, win->dstRect->y, win->dstRect->w, win->dstRect->h)) {
                return true;
            } else {
                return false;
            }
        };

        win->activated = [this,i] {
            return this->instrument->effects.plugins[i]->windowOpen;
        };

        win->inactive = SDL_Color{30,50,30,255};

        item.win = win;

        Button* proc = new Button(
            renderer
        );

        proc->dstRect = new SDL_FRect{
            eRect->x + eRect->w  - (5 * plugMarginX),
            pluginY + plugMarginY,
            plugMarginX * 4,
            pluginHeight - plugMarginY
        };

        proc->onClick = [this,i] {
            this->instrument->effects.plugins[i]->toggle();
        };

        proc->hover = [this, proc] {
            if (inside(this->mouseX, this->mouseY, proc->dstRect->x, proc->dstRect->y, proc->dstRect->w, proc->dstRect->h)) {
                return true;
            } else {
                return false;
            }
        };

        proc->activated = [this,i] {
            return this->instrument->effects.plugins[i]->processing;
        };

        item.proc = proc;

        ePlugs.push_back(item);

        pluginY += pluginHeight;

        i++;
    }

}
