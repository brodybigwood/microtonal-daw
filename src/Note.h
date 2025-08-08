#ifndef NOTE_H
#define NOTE_H

#include <SDL3/SDL.h>
#include <memory>
#include "fract.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class Note {
    public:
        Note(fract start, fract end, float num, double temperament);
        ~Note();

        float num;  // Frequency or pitch of the note
        fract start; //bars
        fract end;

        int id;
        int channel;

        double temperament;
        
        void move(fract x, fract y);

        json toJSON();
        static std::shared_ptr<Note> fromJSON(json&);

};

#endif
