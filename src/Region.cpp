#include "Region.h"
#include <set>
#include <functional>
#include "EventManager.h"
#include "Project.h"
#include "fract.h"

using namespace DAW;

Region::Region() {

}

Region::~Region() {

}

bool Region::updateNoteChannel(std::shared_ptr<Note> n) {
    std::set<int> usedChannels;

    static std::function<int(fract)> toMS = [](fract f) {
        return static_cast<int>(60000.0 * f.num / f.den / Project::instance()->tempo);
    };


    for (std::shared_ptr<Note> other : notes) {
        bool overlap = !(toMS(n->end) + releaseMS <= toMS(other->start) || toMS(n->start) >= toMS(other->end) + releaseMS);
        if (overlap) {
            usedChannels.insert(other->channel);
        }
    }

    for (int ch = 1; ch <= 15; ++ch) {
        if (!usedChannels.count(ch)) {
            n->channel = ch;
            return true;
        }
    }

    return false;
}

