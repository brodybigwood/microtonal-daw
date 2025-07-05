#pragma once
#include <vector>
#include "fract.h"
class fract;
class Instrument;

class GridElement {
    public:

        GridElement();
        ~GridElement();

        struct Position {
            fract startOffset;
            fract start;
            fract end;
            fract length;
            Instrument* instrument;
            int id;
        };

        void createPos(fract, Instrument*);

        std::vector<Position> positions;
};
