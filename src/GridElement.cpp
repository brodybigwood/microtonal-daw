#include "GridElement.h"
#include "fract.h"
#include "Instrument.h"

GridElement::GridElement() {
}

void GridElement::createPos(fract startTime, Instrument* inst) {
    static int lastId = 0;
    int id = lastId++;
    Position pos{
        fract{},
        startTime,
        fract{4,1} + startTime,
        fract{4,1},
        inst,
        id
    };
    positions.push_back(pos);
}

GridElement::~GridElement() {

}
