#include "PianoRoll.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <cmath>
#include <cfloat>
#include "GridView.h"
#include "Region.h"
#include "Note.h"
#include "Playhead.h"
#include "Transport.h"
#include "TuningTable.h"

void PianoRoll::newTuning() {
    delete tuning_table;
    tuning_table = new TuningTable(true);
    updateLines();
}

void PianoRoll::updateLines() {
    lines = tuning_table->notes;
    Scroll();
}

PianoRoll::PianoRoll(SDL_FRect* rect, std::shared_ptr<DAW::Region> region, bool* detached) : region(region), GridView(detached, rect, 40) {

    tuning_table = new TuningTable(false);
    updateLines();

    if(!detached) {
        WindowHandler::instance()->home->pianoRoll = this;
    }
    SDL_SetCursor(cursors.grabber);

    this->project = Project::instance();

    scrollY = 800;

    divHeight = 200; //octaveheight

    minHeight = 12.0f / 128;

    bottomMargin = 20;

    UpdateGrid();

    Scroll();

    initWindow();

    float x = -1000; //for now only this many measures
    times.clear();
    while(x < 1000) {
        times.push_back(x);
        x += 1.0f/notesPerBar;
    }

    createGridRect();
}

PianoRoll::~PianoRoll() {
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
    cellHeight = divHeight/notesPerOctave;
    cellHeight12 = divHeight/12.0;

    double a440 = cellHeight12*59;
    yMin = cellHeight12*59 - std::floor(cellHeight12*59/cellHeight)*cellHeight;
    yMax = cellHeight12*69 - std::floor(cellHeight12*69/cellHeight)*cellHeight;

    Scroll();
}

double PianoRoll::getNoteName(double y) {
    return 129-(y + scrollY)/cellHeight12;
}

float PianoRoll::getY(float noteMidiNum) {
    return -cellHeight12*((noteMidiNum-129)+(scrollY/cellHeight12)) - lineWidth;
}

float PianoRoll::getHoveredLine() {
    float closestDiff = FLT_MAX;
    float closestLine = -1.0f;

    for (auto line : lines) {
        float y = getY(line);
        float diff = std::abs(mouseY - y);
        if (diff < closestDiff) {
            closestDiff = diff;
            closestLine = line;
        }
    }

    return closestLine;
}


void PianoRoll::RenderDestinations() {

    if (fonts.mainFont) {
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: mainFont is NULL in PianoRoll::RenderDestinations!\n");
        return;
    }


    SDL_Texture* KeyTexture;
    SDL_SetRenderTarget(renderer, PianoTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0); // Transparent
    SDL_RenderClear(renderer);
    SDL_Color textColor = {0, 0, 0, 255};


    SDL_FRect backgroundRect = {0, topMargin, leftMargin, height - topMargin - bottomMargin};

    setRenderColor(colors.keyWhite);
    SDL_RenderFillRect(renderer, &backgroundRect);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);
    SDL_RenderLine(renderer, leftMargin+1,topMargin,leftMargin+1,height - topMargin - bottomMargin);

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);

    for (double y = yOffset12-cellHeight12; y < height+cellHeight12; y += cellHeight12) {

        double noteNum = std::abs(getNoteName(y)-1);
        
        std::string noteNumStrTemp = std::to_string(noteNum);
        const char* noteNumStr = noteNumStrTemp.c_str();
        
        SDL_Surface* textSurface = TTF_RenderText_Solid(fonts.mainFont, noteNumStr, 3, textColor);  // textColor is an SDL_Color

        KeyTexture = SDL_CreateTextureFromSurface(renderer, textSurface);

        SDL_FRect textRect = {
            0,
            static_cast<float>(y + cellHeight12) - static_cast<float>(textSurface->h)/2,
            static_cast<float>(textSurface->w),
            static_cast<float>(textSurface->h)
        };

        SDL_RenderLine(renderer, textSurface->w, y, leftMargin, y);
        SDL_DestroySurface(textSurface);

        SDL_RenderTexture(renderer, KeyTexture, NULL, &textRect);

    }

    SDL_DestroyTexture(KeyTexture);
        


}

void PianoRoll::Scroll() {


    numCellsRight = (scrollX)/dW;
    numCellsDown = (scrollY-yMin + topMargin)/cellHeight;
    numCellsDown12 = scrollY/cellHeight12;
    if((scrollY + topMargin -yMin - cellHeight12) <= 0) {
        scrollY = yMin + cellHeight12 - topMargin;
    } else {
        if(scrollY+height+yMin+yMax >= 129*cellHeight12) {
            scrollY = 129*cellHeight12 - height - yMin -yMax;
        }
    }
                numCellsDown = (scrollY-yMin)/cellHeight;


    yOffset = (std::ceil(numCellsDown) * cellHeight) - scrollY;
    yOffset12 = (std::ceil(numCellsDown12) * cellHeight12) - scrollY;

    xOffset = (std::ceil(numCellsRight) * dW) - scrollX;

        refreshGrid = true;
        handleMouse();
}

bool PianoRoll::customTick() {
    //refreshGrid = true;
    //handleCustomInput(e);
    if(refreshGrid) {
        refreshGrid = false;
        RenderGridTexture();

        RenderDestinations();
        RenderNotes();

    }

    SDL_SetRenderTarget(renderer, NULL);

    SDL_RenderTexture(renderer, backgroundTexture, nullptr, dstRect);
    SDL_RenderTexture(renderer, gridTexture, nullptr, dstRect);
    SDL_RenderTexture(renderer, NotesTexture, nullptr, dstRect);

    if(project->processing) {
       for(auto pos : region->positions) {
           playHead->render(renderer, dW, scrollX + pos.start * dW);
        }
    }

    SDL_RenderTexture(renderer, PianoTexture, nullptr, dstRect);

    transport->render();

    SDL_FRect bottomRect{
        0,
        height-bottomMargin,
        width,
        bottomMargin
    };

    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &bottomRect);

    return true;
}

void PianoRoll::initWindow() {

    dstRect->w = width;
    dstRect->h = height;

    gridRect = {
        dstRect->x+leftMargin,
        dstRect->y+topMargin,
        dstRect->w-leftMargin,
        dstRect->h-topMargin - bottomMargin
    };

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(gridTexture);
    SDL_DestroyTexture(PianoTexture);
    SDL_DestroyTexture(KeyTexture);
    SDL_DestroyTexture(NotesTexture);


    backgroundTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    gridTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    PianoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    KeyTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    NotesTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);

    layers[0] = backgroundTexture;
    layers[1] = gridTexture;
    layers[2] = NotesTexture;
    layers[3] = PianoTexture;

    SDL_SetRenderTarget(renderer, backgroundTexture);
    setRenderColor(colors.background);

    SDL_RenderClear(renderer); // Clear backgroundTexture with the background color

    SDL_SetRenderTarget(renderer, NULL);

    if(height > (128*cellHeight12 - yMax - yMin)) {
        divHeight = 12*height/128;
        UpdateGrid();
        
    }
    
    Scroll();
    RenderGridTexture();      

    RenderDestinations();
    
    RenderNotes();

}

void PianoRoll::clickMouse(SDL_Event& e) {
    switch (e.type) {

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = true;
                if(mouseY < topMargin) {
                    return;
                }
                if(mouseY > height - bottomMargin) {
                    newTuning();
                    return;
                }
                if(mouseX > leftMargin && stretchingNote == nullptr) {
                    if(hoveredElement == nullptr) {
                        createElement();
                    } else {
                        notesPerOctave = std::dynamic_pointer_cast<Note>(hoveredElement)->temperament;
                        UpdateGrid();
                        Scroll();
                        movingNote = std::dynamic_pointer_cast<Note>(hoveredElement);
                    }
                }
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = true;
                if(mouseX > leftMargin && stretchingNote == nullptr) {
                    deleteElement();
                }

            }
            handleMouse();
            refreshGrid = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                lmb = false;
                movingNote = nullptr;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                rmb = false;
            }
            handleMouse();
            break;
    }
}

void PianoRoll::handleCustomInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            Scroll();
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            width = e.window.data1;  // New width
            height = e.window.data2; // New height

            initWindow();
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            width = e.window.data1;  // New width
            height = e.window.data2; // New height

            initWindow();
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
                default:
                    break;
            }
            break;

        

        case SDL_EVENT_MOUSE_MOTION:
            moveMouse();

            handleMouse();

            
            if(stretchingNote != nullptr) {
                if(!lmb) {
                    isStretchingNote = false;
                } else {
                    refreshGrid = true;
                    float dX = mouseX - last_lmb_x;
                    if(dX >= dW/notesPerBar) {
                        stretchElement(1);
                        last_lmb_x += dW/notesPerBar;
                    } else if(dX <= -dW/notesPerBar) {
                        stretchElement(-1);
                        last_lmb_x -= dW/notesPerBar;
                    }
                }

            } else if(lmb && movingNote != nullptr) {
                refreshGrid = true;
                float dX = mouseX - last_lmb_x;
                float dirX = std::abs(dX)/dX;

                float dY = mouseY - last_lmb_y;
                float dirY = std::abs(dY)/dY;

                if(std::abs(dX) >= dW/notesPerBar) {
                    moveNote(movingNote, std::ceil(dirX*dX)/dX,0);
                    last_lmb_x += dW*dirX/notesPerBar;
                } if (getHoveredLine() != movingNote->num) {
                    moveNote(movingNote, 0,getHoveredLine() - movingNote->num);
                    lastHoveredLine = getHoveredLine();
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

void PianoRoll::createElement() {
    fract start = getHoveredTime();
    float pitch = getHoveredLine();
    std::cout<<pitch<<std::endl;
    auto n = std::make_shared<Note>(start, lastLength + start, pitch, notesPerOctave);

    static int nextId = 0;
    n->id = nextId++;

    region->updateNoteChannel(n);

    region->notes.push_back(n);

    refreshGrid = true;
}

void PianoRoll::RenderNotes() {
    SDL_SetRenderTarget(renderer, NotesTexture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

    //backgrounds first
    for(std::shared_ptr<Note> note : region->notes) {

        float noteX = getNotePosX(note) +1;
        float noteY = getY(note->num);
        float noteEnd = getNoteEnd(note) -2;
        float noteTop = noteY + getNoteHeight(note);


        setRenderColor(colors.noteBackground);
        SDL_FRect noteBGRect = { noteX, noteY, noteEnd - noteX, noteTop-noteY};
        SDL_RenderFillRect(renderer, &noteBGRect);
    }

    for(std::shared_ptr<Note> note : region->notes) {
            float noteX = getNotePosX(note) +1;
            float noteY = getY(note->num);
            float noteEnd = getNoteEnd(note) -2;
            float noteTop = noteY + getNoteHeight(note);

            //noteRadius = (noteTop - noteY)/2;

            setRenderColor(colors.note);
            SDL_FRect noteRect = { noteX, noteY - noteRadius, noteEnd - noteX, 2*noteRadius};
            SDL_RenderFillRect(renderer, &noteRect);

            setRenderColor(colors.noteBorder);

            SDL_RenderRect(renderer, &noteRect);

    }

}

bool PianoRoll::getExistingNote() {
    hoveredElement = nullptr;
    if(mouseY < topMargin || mouseX < leftMargin) {
        return false;
    }
    int i = 0;
    for (std::shared_ptr<Note> note : region->notes) {
        
        // Get the required positions and size once per iteration
        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(note->num);
        
        // Check if mouse is within note bounds
        if (mouseX >= notePosX && mouseX <= noteEnd &&
            mouseY <= noteY + noteRadius && mouseY >= (noteY - noteRadius)) {
            hoveredElement = note; // Found the hovered note
            lastHoveredLine = getHoveredLine();
            return true; // Exit early
        }
        i++;
    }
    return false;
}


float PianoRoll::getNotePosX(std::shared_ptr<Note> note) {
    return getX(note->start);
}

float PianoRoll::getNoteEnd(std::shared_ptr<Note> note) {
    return getX(note->end);
}

float PianoRoll::getNoteHeight(std::shared_ptr<Note> note) {
    return -cellHeight12*12/note->temperament + lineWidth;
}

void PianoRoll::deleteElement() {
    if(hoveredElement != nullptr) {
        auto& notes = region->notes;
        auto it = std::find(notes.begin(), notes.end(), hoveredElement);
        if (it != notes.end()) {
            notes.erase(it);
        }
        Scroll();
    }

}


void PianoRoll::handleMouse() {
    getStretchingNote();
    getExistingNote();

    if(rmb) {
        SDL_SetCursor(cursors.pencil);
        if(hoveredElement != nullptr) {
            deleteElement();
        } 
    } else {
        if (stretchingNote != nullptr) {
            SDL_SetCursor(cursors.resize);
        } else if(hoveredElement != nullptr) {
            SDL_SetCursor(cursors.mover);
        } else {
            SDL_SetCursor(cursors.selector);
        }
    }
}

bool PianoRoll::getStretchingNote() {
    if(movingNote != nullptr) {
        return false;
    }
    if(isStretchingNote) {
        return true;
    }
    for (std::shared_ptr<Note> note : region->notes) {
        const int notePosX = getNotePosX(note);
        const int noteEnd = getNoteEnd(note);
        const int noteY = getY(note->num);

        if ((mouseY >= noteY - noteRadius && mouseY <= (noteY + noteRadius))) {
            if(mouseX >= notePosX - selectThresholdX/2 && mouseX <= notePosX + selectThresholdX/2) {
                stretchingNote = note;
                resizeDir = -1;
                isStretchingNote = true;
                return true;
            } else if (mouseX >= noteEnd - selectThresholdX/2 && mouseX <= noteEnd + selectThresholdX/2) {
                stretchingNote = note;
                resizeDir = 1;
                isStretchingNote = true;
                return true;
            }
        }
    }
    stretchingNote = nullptr;
    isStretchingNote = false;
    return false;
}

void PianoRoll::stretchElement(int amount) {
    if(stretchingNote == nullptr) {
        return;
    }
    if(resizeDir == -1) {
        stretchingNote->start = stretchingNote->start + fract(amount,notesPerBar);
    } else if(resizeDir == 1) {
        stretchingNote->end = stretchingNote->end + fract(amount,notesPerBar);
    }

    if(stretchingNote->end < stretchingNote->start) {
        stretchingNote->end = stretchingNote->start;
    }
    lastLength = stretchingNote->end - stretchingNote->start;
    Scroll();
}


void PianoRoll::moveNote(std::shared_ptr<Note> note, int moveX, float y) {
    fract x = fract(moveX, notesPerBar);

    note->start = note->start + x;
    note->end = note->end + x;
    note->num = note->num + y;

    region->updateNoteChannel(note);
    Scroll();
}

