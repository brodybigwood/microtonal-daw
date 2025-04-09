#include <iostream>
#include <vector>
#include "Note.h"
#include "fract.h"

#ifndef REGION_H
#define REGION_H


class Region {
    private:
        

    public:
        Region(fract startTime, int y);
        ~Region();

 std::vector<Note> notes; 

 void createNote(fract, fract);

 void sort();

 fract startTime;

 int y;

 fract length = fract(4,1);

 bool userSetRight = false;
 bool userSetLeft = false;

 void moveX(fract);
 void moveY(int);
 void resize(bool, fract);
};


#endif