
#include <SDL_ttf.h>
#include "GridView.h"
#include "Region.h"
#include "Note.h"
#include "fract.h" 
#include "styles.h"
#include "Project.h"

#ifndef PIANOROLL_H
#define PIANOROLL_H

class PianoRoll : public GridView {

    public:



    PianoRoll(int, int, DAW::Region*);
    virtual ~PianoRoll();
    
        SDL_Texture* backgroundTexture;
        SDL_Texture* gridTexture;
        SDL_Texture* PianoTexture;
        SDL_Texture* NotesTexture;
        SDL_Texture* KeyTexture;

        SDL_FRect textRect;

        SDL_Texture* keys[128];

        DAW::Region* region;

        double octaveHeight = 200;

        double referenceOffset;

        double notesPerOctave = 12;
        double cellHeight12;

        double yOffset12;

        double numCellsDown12;

        double numCellsRight;
        double numCellsDown;

 

        bool refreshGrid = false; 
        bool refreshNotes = false;

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
        void handleCustomInput(SDL_Event&) override;
        void toggleKey(SDL_Event&, SDL_Scancode, bool&);
        void initWindow();

        void Scroll();

        void createElement() override;

        void deleteNote(int);

        bool getExistingNote();

        double getNoteName(double);
        double getY(double);

        double getX(double);

        
        fract lastLength = fract(1, 1);

        SDL_Texture* layers[4]; 

        fract getHoveredLine();
        void handleMouse();

        float getNotePosX(Note&);
        float getNoteEnd(Note&);
        float getNoteHeight(Note&);

        void moveNote(int, int, int);
        
    private:
        SDL_FRect gridRect;

        float keyLength = 40;
        float menuHeight = 0;

        bool getStretchingNote();
        void stretchNote(int amount);

        float selectThresholdX = 5.0f;
        float selectThresholdY = 5.0f;

        Note* stretchingNote = nullptr;
        int resizeDir;
        bool isStretchingNote = false;

        float noteRadius = 5;
};

#endif
