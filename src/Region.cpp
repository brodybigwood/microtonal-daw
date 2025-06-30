#include "Region.h"
#include <set>
#include <functional>
#include "EventManager.h"
#include "Project.h"

using namespace DAW;

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

    static std::function<int(fract)> toMS = [](fract f) {
        return static_cast<int>(60000.0 * f.num / f.den / Project::instance()->tempo);
    };


    for (const auto& other : notes) {
        bool overlap = !(toMS(n.end) + releaseMS <= toMS(other.start) || toMS(n.start) >= toMS(other.end) + releaseMS);
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

