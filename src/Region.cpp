#include "Region.h"


Region::Region(fract startTime, float y) {
    this->startTime = startTime;
    this-> y = y;



}

Region::~Region() {

}

void Region::moveX(fract dx) {
    startTime = startTime + dx;
}

void Region::moveY(int dy) {
    y = y + dy;
}

void Region::resize(bool rightSide, fract dS) {

    if(rightSide) {
        userSetRight = true;
        length = length + dS;
    } else {
        userSetLeft = true;
        length = length - dS;
        startTime = startTime + dS;
    }
}

