#include "Region.h"
#include <set>

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

bool Region::updateNoteChannel(Note& n) {
    std::set<int> usedChannels;

    for (const auto& other : notes) {
        bool overlap = !(n.end <= other.start || n.start >= other.end);
        if (overlap) {
            usedChannels.insert(other.channel);
        }
    }

    for (int ch = 2; ch <= 15; ++ch) {
        if (!usedChannels.count(ch)) {
            n.channel = ch;
            return true;
        }
    }

    return false;
}

