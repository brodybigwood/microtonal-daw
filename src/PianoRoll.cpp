#include "PianoRoll.h"

#include <cmath>

#include "Region.h"
#include "Note.h"



PianoRoll::PianoRoll(int windowWidth, int windowHeight, Region& region) : region(region) {


    this->windowWidth = windowWidth;
    this->windowHeight = windowHeight;

    window = SDL_CreateWindow("Piano Roll", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);

        renderer = SDL_CreateRenderer(window, NULL);



    UpdateGrid();


    Scroll();
    initWindow();


}

PianoRoll::~PianoRoll() {
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    for(int i = 0; i<4; i++) {
        SDL_DestroyTexture(layers[i]);
    }
    std::cout<<"closing iwndow"<<std::endl;

}



void PianoRoll::UpdateGrid() {
    gridHeight = octaveHeight/notesPerOctave;
    gridWidth = barWidth/notesPerBar;
    gridHeight12 = octaveHeight/12.0;

    double a440 = gridHeight12*59;
    yMin = gridHeight12*59 - std::floor(gridHeight12*59/gridHeight)*gridHeight;
    yMax = gridHeight12*69 - std::floor(gridHeight12*69/gridHeight)*gridHeight;

}

fract PianoRoll::getHoveredTime() {
    return fract(std::floor((mouseX+scrollX)/gridWidth),notesPerBar);
}

double PianoRoll::getX(double grid) {
    return grid * gridWidth;
}

double PianoRoll::getNote(double y) {
    return 128-(y + scrollY)/gridHeight12;
}

double PianoRoll::getY(double noteMidiNum) {
    return -gridHeight12*((noteMidiNum-128)+(scrollY/gridHeight12)) - lineWidth;
}

fract PianoRoll::getHoveredNote() {
    fract result = fract((notesPerOctave*128)-std::ceil(numCellsDown+((mouseY+yMin)/gridHeight))*12,notesPerOctave);
    std::cout<<result.num/result.den<<std::endl;
    return result;
}

void PianoRoll::RenderKeys() {
        SDL_Texture* KeyTexture;
        SDL_SetRenderTarget(renderer, PianoTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);
    SDL_Color textColor = {0, 0, 0, 255};
    font = TTF_OpenFont("assets/fonts/Arial.ttf", 12);





            SDL_FRect backgroundRect = {0, 0, keyLength, windowHeight};

        setRenderColor(renderer, colors.keyWhite);
SDL_RenderFillRect(renderer, &backgroundRect); // Render the background rectangle


        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255); // RGBA

    for (double y = yOffset12-gridHeight12; y < windowHeight+gridHeight12; y += gridHeight12) {


             //SDL_RenderLine(renderer, 0, y, 20, y);


        double noteNum = std::abs(getNote(y)-1);
        
        const char* noteNumStr = std::to_string(noteNum).c_str();

        SDL_Surface* textSurface = TTF_RenderText_Solid(font, noteNumStr, 3, textColor);  // textColor is an SDL_Color

        KeyTexture = SDL_CreateTextureFromSurface(renderer, textSurface);

        SDL_FRect textRect = {0, (int)y, textSurface->w, textSurface->h};  // Position at x, y
        SDL_DestroySurface(textSurface);  // Free the surface after creating the texture
        // Render the text directly onto PianoTexture


        SDL_RenderTexture(renderer, KeyTexture, NULL, &textRect);

        // Destroy the text texture after use
        //
    }

    SDL_DestroyTexture(KeyTexture);
        


    TTF_CloseFont(font);
}


void PianoRoll::RenderRoll() {
    SDL_SetRenderTarget(renderer, NULL);
    //std::cout<<"abt to render layers"<<std::endl;
    for(int i = 0; i<4; i++) {
        SDL_RenderTexture(renderer, layers[i], NULL, NULL);
    }

}



void PianoRoll::Scroll() {


    numCellsRight = scrollX/gridWidth; 
    numCellsDown = (scrollY-yMin)/gridHeight;
    numCellsDown12 = scrollY/gridHeight12;
    if((scrollY-yMin) <= 0) {
        scrollY = yMin;
    } else {
        if(scrollY+windowHeight+yMax >= 128*gridHeight12) {
            scrollY = 128*gridHeight12 - windowHeight - yMax;
        }
    }
                numCellsDown = (scrollY-yMin)/gridHeight;
            std::cout<<"bottom :"<<numCellsDown+(windowHeight/gridHeight)<<"; top: "<<numCellsDown<<std::endl;


    yOffset = (std::ceil(numCellsDown) * gridHeight) - scrollY;
    yOffset12 = (std::ceil(numCellsDown12) * gridHeight12) - scrollY;

    xOffset = (std::ceil(numCellsRight) * gridWidth) - scrollX;

        refreshGrid = true;
  
}


void PianoRoll::RenderGridTexture() {


       std::cout<<"rendering grid texture"<<std::endl;

    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);

    setRenderColor(renderer, colors.grid);

    std::cout<<"about to draw vertical grid texture"<<std::endl;
    
    for (double x = xOffset; x < windowWidth; x += gridWidth) {
        SDL_RenderLine(renderer, x, 0, x, windowHeight);
    }
    std::cout<<"about to draw  horizontal grid texture"<<std::endl;
       std::cout<<"yOffset: "<<yOffset<<", windowHeight: "<<windowHeight<<", gridHeight: "<<gridHeight<<std::endl;
    for (double y = yOffset; y < windowHeight; y += gridHeight) {
        SDL_RenderLine(renderer, 0, y, windowWidth, y);
    }


}

bool PianoRoll::tick() {

    //handleInput(e);
    if(refreshGrid) {
        RenderGridTexture();  
        RenderKeys();
        refreshGrid = false;

        RenderNotes()  ;     
        RenderRoll();

    }








    SDL_RenderPresent(renderer);  
    return running;
}

void PianoRoll::setRenderColor(SDL_Renderer* theRenderer, uint8_t code[4]) {
    SDL_SetRenderDrawColor(theRenderer,
        code[0],
        code[1],
        code[2],
        code[3]
    );  
}

void PianoRoll::initWindow() {

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(gridTexture);
    SDL_DestroyTexture(PianoTexture);
    SDL_DestroyTexture(KeyTexture);
    SDL_DestroyTexture(NotesTexture);


    backgroundTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, windowWidth, windowHeight);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, windowWidth, windowHeight);
    PianoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, windowWidth, windowHeight);
    KeyTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, windowWidth, windowHeight);
    NotesTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, windowWidth, windowHeight);

    layers[0] = backgroundTexture;
    layers[1] = gridTexture;
    layers[2] = NotesTexture;
    layers[3] = PianoTexture;

    SDL_SetRenderTarget(renderer, backgroundTexture);
    setRenderColor(renderer, colors.background);

    SDL_RenderClear(renderer); // Clear backgroundTexture with the background color

    SDL_SetRenderTarget(renderer, NULL);

    if(windowHeight > (128*gridHeight12 - yMax - yMin)) {
        octaveHeight = 12*windowHeight/128;
        UpdateGrid();
        
    }
    Scroll();
    RenderGridTexture();      
    std::cout<<"rendered grid texture"<<std::endl;
    RenderKeys();
    RenderNotes();
    RenderRoll();

}

void PianoRoll::toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar) {
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


void PianoRoll::handleInput(SDL_Event& e) {
    
    refreshGrid = false;
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
                double gridAtX = (mouseX / gridWidth) + (scrollX / gridWidth);
                UpdateGrid();
                scrollX = gridAtX * gridWidth - mouseX;
            } else
            if (isAltPressed) {
                octaveHeight *= std::pow(scaleSensitivity, e.wheel.y);
                if (octaveHeight < windowHeight * 12 / 128 || octaveHeight <= 0) {
                    octaveHeight = windowHeight * 12 / 128;
                }

                double gridAtY = (mouseY / gridHeight) + (scrollY / gridHeight);
                UpdateGrid();
                scrollY = gridAtY * gridHeight - mouseY;
            } else if (isShiftPressed) {
                scrollX -= e.wheel.y * scrollSensitivity;
            } else {
                scrollY -= e.wheel.y * scrollSensitivity;  // Adjust scroll amount based on mouse wheel
            }

            Scroll();
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:


            windowWidth = e.window.data1;  // New width
            windowHeight = e.window.data2; // New height
            initWindow();
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if(mouseX > keyLength) {
                    createNote(getHoveredTime(), getHoveredNote());
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                if(mouseX > keyLength) {
                    deleteNote(getExistingNote());
                }

            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            SDL_GetMouseState(&mouseX, &mouseY);

            if(lmb) {
                
            }

            break;


        // Optionally handle other events you might need:
        default:
            break;
    }
}

void PianoRoll::createNote(fract start, fract pitch) {
        std::cout<<"bar: "<<double(lastLength)<<std::endl;
        
        region.notes.push_back(Note(start, lastLength + start, pitch, notesPerOctave));

        refreshGrid = true;
}

void PianoRoll::RenderNotes() {
    SDL_SetRenderTarget(renderer, NotesTexture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);
    for(int i = 0; i<region.notes.size(); i++) {
        Note& note = region.notes[i];
            std::cout<<"note length: "<<getNoteEnd(note)<<std::endl;
            float noteX = getNotePosX(note) +1;
            float noteY = getY(note.num) -1 ;
            float noteEnd = getNoteEnd(note) -2;
            float noteTop = noteY + getNoteHeight(note) +2;

            setRenderColor(renderer, colors.note);
            SDL_FRect noteRect = { noteX, noteY, noteEnd - noteX, noteTop - noteY};
            SDL_RenderFillRect(renderer, &noteRect);

            setRenderColor(renderer, colors.noteBorder);

            SDL_RenderLine(renderer, noteX, noteY, noteEnd, noteY);
            SDL_RenderLine(renderer, noteX, noteTop, noteEnd, noteTop);

            SDL_RenderLine(renderer, noteEnd, noteY, noteEnd, noteTop);
            SDL_RenderLine(renderer, noteX, noteTop, noteEnd, noteTop);
            std::cout<<"rendered note with num "<<note.num<<" at "<<note.start*barWidth<<std::endl;

    }
    SDL_SetRenderTarget(renderer, NULL);
SDL_RenderTexture(renderer, NotesTexture, NULL, NULL);

}

int PianoRoll::getExistingNote() {
    for(int i = 0; i<region.notes.size(); i++) {
        std::cout<<i<<std::endl;
        Note& note = region.notes[i];
        if(
            mouseX >= getNotePosX(note) &&
            mouseX <= getNoteEnd(note) &&
            mouseY <= getY(note.num) &&
            mouseY >= getY(note.num) + getNoteHeight(note)
        ) {
            return i;
        }
    }
    return -1;
}

float PianoRoll::getNotePosX(Note& note) {
    return note.start*barWidth -scrollX;
}

float PianoRoll::getNoteEnd(Note& note) {
    return note.end*barWidth - scrollX;
}

float PianoRoll::getNoteHeight(Note& note) {
    return -gridHeight12*12/note.temperament + lineWidth;
}

void PianoRoll::deleteNote(int index) {
    if(index != -1) {
        region.notes.erase(region.notes.begin() + index); 
        std::cout<<"deleted succ"<<std::endl;
        Scroll();
    }

}


