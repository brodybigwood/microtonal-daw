#include "SongRoll.h"


SongRoll::SongRoll(int x, int y, int width, int height, SDL_Renderer* renderer, Project* project, WindowHandler* windowHandler) {

    this->windowHandler = windowHandler;
    this->project = project;
    this->width = width;
    this->height = height;
    this->renderer = renderer;

    this->x = x;
    this->y = y;

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    regionTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    playHeadTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    dstRect = {x, y, width, height};


    playHead = new Playhead(renderer, playHeadTexture, project, windowHandler);
    
}

void SongRoll::render() {
    RenderGridTexture();
    renderRegions();
    playHead->render(barWidth);
    
    SDL_SetRenderTarget(renderer,texture);
    SDL_SetRenderDrawColor(renderer, colors.background[0], colors.background[1], colors.background[2], colors.background[3]);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer,NULL);
    SDL_RenderTexture(renderer, texture, nullptr, &dstRect);

    SDL_RenderTexture(renderer,gridTexture,nullptr, &dstRect);
    SDL_RenderTexture(renderer,regionTexture,nullptr, &dstRect);

    SDL_RenderTexture(renderer,playHeadTexture,nullptr, &dstRect);
}


SongRoll::~SongRoll() {

}

void SongRoll::toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar) {
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        if (e.key.scancode == keycode) {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                keyVar = true;  // Shift key is pressed
            } else if (e.type == SDL_EVENT_KEY_UP) {
                keyVar = false;  // Shift key is released
            }
        }
    }
}


void SongRoll::handleInput(SDL_Event& e) {
    

    toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
    toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
    toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);
 
    // Handle different events with a switch statement
    
    switch (e.type) {

            

        case SDL_EVENT_MOUSE_WHEEL:
            if (isCtrlPressed) {
                barWidth *= std::pow(scaleSensitivity, e.wheel.y);
                if (barWidth <= 4) {
                    barWidth = 4;
                }
                double gridAtX = (mouseX / cellWidth) + (scrollX / cellWidth);
                scrollX = gridAtX * cellWidth - mouseX;
            } else
            if (isAltPressed) {
                cellHeight *= std::pow(scaleSensitivity, e.wheel.y);
                if (cellHeight < height * 12 / 128 || cellHeight <= 0) {
                    cellHeight = height * 12 / 128;
                }

                double gridAtY = (mouseY / cellHeight) + (scrollY / cellHeight);
                scrollY = gridAtY * cellHeight - mouseY;
            } else if (isShiftPressed) {
                scrollX -= e.wheel.y * scrollSensitivity;
            } else {
                scrollY -= e.wheel.y * scrollSensitivity;  // Adjust scroll amount based on mouse wheel
            }

            break;



        




        // Optionally handle other events you might need:
        default:
            break;
    }
}

void SongRoll::RenderGridTexture() {




    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 
        colors.grid[0],
        colors.grid[1],
        colors.grid[2],
        colors.grid[3]
    );

    
    for (double x = 0; x < width; x += cellWidth) {
        SDL_RenderLine(renderer, x, 0, x, height);
    }

    
    for (double y = 0; y < height; y += cellHeight) {
        SDL_RenderLine(renderer, 0, y, width, y);
    }


}

void SongRoll::renderRegions() {
    for(size_t i = 0; i<project->regions.size(); i++) {
        renderRegion(i);
    }
}

void SongRoll::renderRegion(int r) {
    if(hoveredRegion == r) {
        SDL_SetRenderDrawColor(renderer, 90,90,100,127);
    } else {
        SDL_SetRenderDrawColor(renderer, 20,20,100,127);
    }

    Region region = project->regions[r];

    float topLeftCornerX = region.startTime*barWidth;
    float topLeftCornerY = region.y*cellHeight;
    regionRect = {topLeftCornerX, topLeftCornerY, region.length*barWidth, cellHeight};
    SDL_SetRenderTarget(renderer, regionTexture);

    SDL_RenderFillRect(renderer, &regionRect);
}

void SongRoll::getHoveredRegion() {
    for(size_t i = 0; i<project->regions.size(); i++) {
        Region region = project->regions[i];
        if(
            mouseX > region.startTime*barWidth &&
            mouseX < (region.length+region.startTime)*barWidth &&
            mouseY > region.y*cellHeight &&
            mouseY < (region.y+1) * cellHeight
        ) {
            hoveredRegion = i;
            return;
        } else {
            hoveredRegion = -1;
        }
    }
}


void SongRoll::moveMouse(float x, float y) {
    mouseX = x;
    mouseY = y;
    getHoveredRegion();
}

void SongRoll::clickMouse(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if(hoveredRegion != -1) {
                    windowHandler->createPianoRoll(project->regions[hoveredRegion]);
                    project->setViewedElement("region", hoveredRegion);
                    hoveredRegion = -1;
                }

            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = false;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = false;
            }
            break;       
    }
}