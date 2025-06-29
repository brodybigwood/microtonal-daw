#include "PianoRoll.h"

#include <SDL3/SDL_scancode.h>
#include <cmath>

#include "Region.h"
#include "Note.h"
#include "Playhead.h"



PianoRoll::PianoRoll(int windowWidth, int windowHeight, DAW::Region* region) : region(region) {

    SDL_SetCursor(cursors.grabber);
    this->windowWidth = windowWidth;
    this->windowHeight = windowHeight;

    window = SDL_CreateWindow("Piano Roll", windowWidth, windowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);

        renderer = SDL_CreateRenderer(window, NULL);

    this->project = Project::instance();

    this->playHead = new Playhead(this->project, &gridRect);

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

}



void PianoRoll::UpdateGrid() {
    if(notesPerOctave <= 0) {
        notesPerOctave = 1;
    } else if(notesPerOctave > 128) {
        notesPerOctave = 128;
    }
    cellHeight = octaveHeight/notesPerOctave;
    cellWidth = barWidth/notesPerBar;
    cellHeight12 = octaveHeight/12.0;

    double a440 = cellHeight12*59;
    yMin = cellHeight12*59 - std::floor(cellHeight12*59/cellHeight)*cellHeight;
    yMax = cellHeight12*69 - std::floor(cellHeight12*69/cellHeight)*cellHeight;

    Scroll();
}

fract PianoRoll::getHoveredTime() {
    return fract(std::floor((mouseX+scrollX)/cellWidth),notesPerBar);
}

double PianoRoll::getX(double grid) {
    return grid * cellWidth;
}

double PianoRoll::getNoteName(double y) {
    return 129-(y + scrollY)/cellHeight12;
}

double PianoRoll::getY(double noteMidiNum) {
    return -cellHeight12*((noteMidiNum-129)+(scrollY/cellHeight12)) - lineWidth;
}

fract PianoRoll::getHoveredCell() {
    fract result = fract((notesPerOctave*128)-std::ceil(numCellsDown+((mouseY+yMin)/cellHeight))*12,notesPerOctave) + fract(1,1);

    return result;
}

void PianoRoll::RenderKeys() {

    if (fonts.mainFont) {
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: mainFont is NULL in PianoRoll::RenderKeys!\n");
        return;
    }


        SDL_Texture* KeyTexture;
        SDL_SetRenderTarget(renderer, PianoTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);
    SDL_Color textColor = {0, 0, 0, 255};


            SDL_FRect backgroundRect = {0, 0, keyLength, windowHeight};

        setRenderColor(renderer, colors.keyWhite);
SDL_RenderFillRect(renderer, &backgroundRect); 
SDL_SetRenderDrawColor(renderer,0,0,0,255);
SDL_RenderLine(renderer, keyLength+1,0,keyLength+1,windowHeight);

        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255); 

    for (double y = yOffset12-cellHeight12; y < windowHeight+cellHeight12; y += cellHeight12) {


             SDL_RenderLine(renderer, 0, y, keyLength, y);


        double noteNum = std::abs(getNoteName(y)-1);
        
        std::string noteNumStrTemp = std::to_string(noteNum);
        const char* noteNumStr = noteNumStrTemp.c_str();
        
        SDL_Surface* textSurface = TTF_RenderText_Solid(fonts.mainFont, noteNumStr, 3, textColor);  // textColor is an SDL_Color

        KeyTexture = SDL_CreateTextureFromSurface(renderer, textSurface);

        SDL_FRect textRect = {0, (int)y, textSurface->w, textSurface->h};  // Position at x, y
        SDL_DestroySurface(textSurface);  // Free the surface after creating the texture
        // Render the text directly onto PianoTexture


        SDL_RenderTexture(renderer, KeyTexture, NULL, &textRect);

        // Destroy the text texture after use
        //
    }

    SDL_DestroyTexture(KeyTexture);
        


}


void PianoRoll::RenderRoll() {
    SDL_SetRenderTarget(renderer, NULL);

    for(int i = 0; i<4; i++) {
        SDL_RenderTexture(renderer, layers[i], NULL, NULL);
    }

}



void PianoRoll::Scroll() {


    numCellsRight = scrollX/cellWidth; 
    numCellsDown = (scrollY-yMin)/cellHeight;
    numCellsDown12 = scrollY/cellHeight12;
    if((scrollY-yMin - cellHeight12) <= 0) {
        scrollY = yMin + cellHeight12;
    } else {
        if(scrollY+windowHeight+yMin+yMax >= 128*cellHeight12) {
            scrollY = 128*cellHeight12 - windowHeight - yMin -yMax;
        }
    }
                numCellsDown = (scrollY-yMin)/cellHeight;


    yOffset = (std::ceil(numCellsDown) * cellHeight) - scrollY;
    yOffset12 = (std::ceil(numCellsDown12) * cellHeight12) - scrollY;

    xOffset = (std::ceil(numCellsRight) * cellWidth) - scrollX;

        refreshGrid = true;
        handleMouse();
}


void PianoRoll::RenderGridTexture() {




    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);

    setRenderColor(renderer, colors.grid);


    
    for (double x = xOffset; x < windowWidth; x += cellWidth) {
        SDL_RenderLine(renderer, x, 0, x, windowHeight);
    }

    
    for (double y = yOffset; y < windowHeight; y += cellHeight) {
        SDL_RenderLine(renderer, 0, y, windowWidth, y);
    }


}

bool PianoRoll::tick() {
    //refreshGrid = true;
    //handleInput(e);
    if(refreshGrid) {
        refreshGrid = false;
        RenderGridTexture();  

        RenderKeys();
        RenderNotes();

    }

    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderTexture(renderer, NotesTexture, NULL, NULL);

    RenderRoll();

    playHead->render(renderer, barWidth);

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

    this->gridRect = {
        keyLength,
        menuHeight,
        windowWidth-keyLength,
        windowWidth-menuHeight
    };

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

    if(windowHeight > (128*cellHeight12 - yMax - yMin)) {
        octaveHeight = 12*windowHeight/128;
        UpdateGrid();
        
    }
    
    Scroll();
    RenderGridTexture();      

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
                UpdateGrid();
                scrollX = gridAtX * cellWidth - mouseX;
            } else
            if (isAltPressed) {
                octaveHeight *= std::pow(scaleSensitivity, e.wheel.y);
                if (octaveHeight < windowHeight * 12 / 128 || octaveHeight <= 0) {
                    octaveHeight = windowHeight * 12 / 128;
                }

                double gridAtY = (mouseY / cellHeight) + (scrollY / cellHeight);
                UpdateGrid();
                scrollY = gridAtY * cellHeight - mouseY;
            } else if (isShiftPressed) {
                scrollX += e.wheel.y * scrollSensitivity;
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
                    if(hoveredNote == -1) {
                        createNote(getHoveredTime(), getHoveredCell());
                    } else {
                        notesPerOctave = region->notes[hoveredNote].temperament;
                        UpdateGrid();
                        Scroll();
                        movingNote = hoveredNote;
                    }
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                if(mouseX > keyLength) {
                    deleteNote(hoveredNote);
                }

            }
            handleMouse();
            refreshGrid = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = false;
                movingNote = -1;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = false;
            }
            handleMouse();
            break;

        case SDL_EVENT_KEY_DOWN:

            switch (e.key.scancode) {
                case SDL_SCANCODE_MINUS:
                    notesPerOctave -= 1;
                    UpdateGrid();
                    break;
                case SDL_SCANCODE_EQUALS:
                    notesPerOctave += 1;
                    UpdateGrid();
                    break;
                case SDL_SCANCODE_SPACE:
                    project->togglePlaying();
                    break;
            }
            break;

        

        case SDL_EVENT_MOUSE_MOTION:
            SDL_GetMouseState(&mouseX, &mouseY);

            handleMouse();

            
            if(lmb && movingNote != -1) {
                refreshGrid = true;
                float dX = mouseX - last_lmb_x;
                float dirX = std::abs(dX)/dX;

                float dY = mouseY - last_lmb_y;
                float dirY = std::abs(dY)/dY;

                if(std::abs(dX) >= cellWidth) {
                    moveNote(movingNote, std::ceil(dirX*dX)/dX,0);
                    last_lmb_x += cellWidth*dirX;
                } if (std::abs(dY) >= cellHeight) {
                    moveNote(movingNote, 0,std::ceil(dirY*dY)/dY);
                    last_lmb_y += cellHeight*dirY;
                }
                



            } else {
                last_lmb_x = mouseX;
                last_lmb_y = mouseY;
            }

            break;


        // Optionally handle other events you might need:
        default:
            refreshGrid = false;
            break;
    }
}

void PianoRoll::createNote(fract start, fract pitch) {
        
    Note n(start, lastLength + start, pitch, notesPerOctave);

    static int nextId = 0;
    n.id = nextId++;

    region->updateNoteChannel(n);

    region->notes.push_back(n);

    refreshGrid = true;
}

void PianoRoll::RenderNotes() {
    
    SDL_SetRenderTarget(renderer, NotesTexture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

    for(int i = 0; i<region->notes.size(); i++) {
        Note& note = region->notes[i];

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

    }

}

void PianoRoll::getExistingNote() {
    hoveredNote = -1; // Default to -1, in case no note is found

    for (int i = 0; i < region->notes.size(); i++) {
        Note& note = region->notes[i];
        
        // Get the required positions and size once per iteration
        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(note.num);
        const int noteHeight = getNoteHeight(note);
        
        // Check if mouse is within note bounds
        if (mouseX >= notePosX && mouseX <= noteEnd &&
            mouseY <= noteY && mouseY >= (noteY + noteHeight)) {
            hoveredNote = i; // Found the hovered note
            return; // Exit early
        }
    }
}


float PianoRoll::getNotePosX(Note& note) {
    return note.start*barWidth -scrollX;
}

float PianoRoll::getNoteEnd(Note& note) {
    return note.end*barWidth - scrollX;
}

float PianoRoll::getNoteHeight(Note& note) {
    return -cellHeight12*12/note.temperament + lineWidth;
}

void PianoRoll::deleteNote(int index) {
    if(index != -1) {
        region->notes.erase(region->notes.begin() + index); 

        Scroll();
    }

}


void PianoRoll::handleMouse() {
    getExistingNote();
    if(rmb) {
        SDL_SetCursor(cursors.pencil);
        if(hoveredNote != -1) {
            deleteNote(hoveredNote);
        } 
    } else {
        if(hoveredNote != -1) {
            SDL_SetCursor(cursors.mover);
        } else {
            SDL_SetCursor(cursors.selector);
        }
    }
}


void PianoRoll::moveNote(int noteIndex, int moveX, int moveY) {

    fract x = fract(moveX,notesPerBar);
    fract y = fract(-moveY*12,notesPerOctave);

    Note& note = region->notes[noteIndex];
    note.start = note.start + x;
    note.end = note.end + x;
    note.num = note.num + y;

    region->updateNoteChannel(note);
    Scroll();
}
