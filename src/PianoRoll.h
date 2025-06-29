
#include <SDL_ttf.h>
#include "Region.h"
#include "Note.h"
#include "fract.h" 
#include "styles.h"
#include "Project.h"

#ifndef PIANOROLL_H
#define PIANOROLL_H

class Playhead;

class PianoRoll {

    public:



    PianoRoll(int, int, DAW::Region*);
    virtual ~PianoRoll();
    
        SDL_Window* window;
        SDL_Renderer* renderer;
        SDL_Texture* backgroundTexture;
        SDL_Texture* gridTexture;
        SDL_Texture* PianoTexture;
        SDL_Texture* NotesTexture;
        SDL_Texture* KeyTexture;

        SDL_FRect textRect;

        SDL_Texture* keys[128];

        DAW::Region* region;

        Project* project;

        void setRenderColor(SDL_Renderer*, uint8_t*);

        double barWidth = 80;
        double octaveHeight = 200;

        double referenceOffset;

        double notesPerOctave = 12;
        double notesPerBar = 4;
        double cellHeight12;

        double yOffset12;

        double numCellsDown12;

        double cellWidth;
        double cellHeight;

        int windowWidth;
        int windowHeight;

        float mouseX, mouseY;

        float last_lmb_x, last_lmb_y;

        double scrollX = 0;
        double scrollY = 300; 

        double scaleX = 1;
        double scaleY = 1;

        int scrollSensitivity = 10;
        float scaleSensitivity = 1.1;
        double numCellsRight;
        double numCellsDown;

 
        bool isShiftPressed = false;
        bool isCtrlPressed = false;
        bool isAltPressed = false;
        bool refreshGrid = false; 
        bool refreshNotes = false;
        bool lmb = false; 
        bool rmb = false;

        int yOffset;
        int xOffset;    SDL_Event e;

        double yMin;

        double yMax;
        
        bool running = true;

        int hoveredNote;
        int movingNote;


        bool tick();

        void UpdateGrid();
        void RenderGridTexture();
        

        void createKeys();
        void RenderRoll();
        void RenderKeys();
        void RenderNotes();
        double getMidiNum();
        void handleInput(SDL_Event&);
        void toggleKey(SDL_Event&, SDL_Scancode, bool&);
        void initWindow();

        void Scroll();

        void createNote(fract, fract);

        void deleteNote(int);

        void getExistingNote();

        double getNoteName(double);
        double getY(double);

        fract getHoveredTime();
        double getX(double);

        
        fract lastLength = fract(1, 1);

        SDL_Texture* layers[4]; 

        fract getHoveredCell();
        void handleMouse();

        float getNotePosX(Note&);
        float getNoteEnd(Note&);
        float getNoteHeight(Note&);

        void moveNote(int, int, int);
        
    private:
        Playhead* playHead;
        SDL_FRect gridRect;

        float keyLength = 40;
        float menuHeight = 0;
};

#endif
