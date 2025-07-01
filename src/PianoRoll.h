
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



    PianoRoll(SDL_FRect*, DAW::Region*, bool*);
    virtual ~PianoRoll();
    
        SDL_Texture* backgroundTexture;
        SDL_Texture* gridTexture;
        SDL_Texture* PianoTexture;
        SDL_Texture* NotesTexture;
        SDL_Texture* KeyTexture;

        SDL_FRect textRect;

        SDL_Texture* keys[128];

        DAW::Region* region;

        double referenceOffset;

        double notesPerOctave = 12;
        double cellHeight12;

        double yOffset12;

        double numCellsDown12;

        double numCellsRight;
        double numCellsDown;

        double yMin;
        double yMax;

        int movingNote;

        bool customTick() override;

        void UpdateGrid() override;
        void RenderGridTexture();
        

        void createKeys();
        void RenderRoll();
        void RenderDestinations();
        void RenderNotes();
        double getMidiNum();
        void handleCustomInput(SDL_Event&) override;

        void clickMouse(SDL_Event&) override;

        void toggleKey(SDL_Event&, SDL_Scancode, bool&);
        void initWindow();

        void Scroll();

        void createElement() override;

        void deleteElement() override;

        bool getExistingNote();

        double getNoteName(double);
        double getY(double);

        
        fract lastLength = fract(1, 1);

        SDL_Texture* layers[4]; 

        fract getHoveredLine();
        void handleMouse();

        float getNotePosX(Note&);
        float getNoteEnd(Note&);
        float getNoteHeight(Note&);

        void moveNote(int, int, int);
        
    private:

        bool getStretchingNote();
        void stretchElement(int amount);

        float selectThresholdX = 5.0f;
        float selectThresholdY = 5.0f;

        Note* stretchingNote = nullptr;
        int resizeDir;
        bool isStretchingNote = false;

        float noteRadius = 5;
};

#endif
