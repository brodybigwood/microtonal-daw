#include <iostream>
#include <vector>
#include "Note.h"
#include "fract.h"

#ifndef REGION_H
#define REGION_H


class Region {
    private:
        

    public:
        Region();
        ~Region();
        int size;
        int effectiveSize = 0;
 std::vector<Note> notes; 

 void createNote(fract, fract);

 void sort();

};


#endif