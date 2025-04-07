
#include <SDL_ttf.h>
#include "Region.h"
#include "Note.h"
#include "fract.h" 

#ifndef PIANOROLL_H
#define PIANOROLL_H

class PianoRoll {

    public:



    PianoRoll(int, int, Region&);
    virtual ~PianoRoll();
    
        SDL_Window* window;
        SDL_Renderer* renderer;
        SDL_Texture* backgroundTexture;
        SDL_Texture* gridTexture;
        SDL_Texture* PianoTexture;
        SDL_Texture* NotesTexture;
        SDL_Texture* KeyTexture;


        TTF_Font* font;
        SDL_FRect textRect;

        SDL_Texture* keys[128];

        Region& region;

        float keyLength = 40;

        struct ColorCodes{
            uint8_t background[4];
            uint8_t grid[4];
            uint8_t subGrid[4];

            uint8_t note[4];
            uint8_t noteSelected[4];
            uint8_t noteBorder[4];
            uint8_t noteSelectedBorder[4];

            uint8_t keyText[4];
            uint8_t keyWhite[4];
            uint8_t keyBlack[4];
            uint8_t keyWhiteActive[4];
            uint8_t keyBlackActive[4];

            uint8_t pianoSeparator[4];
            
        };

        int lineWidth = 1;

        ColorCodes colors {
            {66,84,95,255},
            {46,64,75,255},
            {56,74,86,255},

            {200,255,211,255},
            {254,160,161,255},
            {136,176,141,255},
            {187,111,111,255},

            {108,91,93,255},
            {238,242,250,255},
            {76,77,79,255},
            {219,223,231,255},
            {95,96,98,255},

            {50,66,76,255}
        };

        struct Cursors{
            SDL_Cursor* grabber;
            SDL_Cursor* pencil;
            SDL_Cursor* mover;
            SDL_Cursor* selector;
        };

        Cursors cursors{
            SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER),
            SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT),
            SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE),
            SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT),
        };

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

        double getNote(double);
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
        
};

#endif