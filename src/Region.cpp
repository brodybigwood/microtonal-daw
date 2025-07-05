#include "Region.h"
#include <set>
#include <functional>
#include "EventManager.h"
#include "Project.h"
#include "fract.h"
#include "styles.h"

using namespace DAW;

Region::Region() {

}

Region::~Region() {

}

void Region::draw(SDL_Renderer* renderer) {

    if(!texture) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 10000, 100);
    }

    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

    for(std::shared_ptr<Note> note : notes) {
        float noteX = note->start * 100;
        float noteY = (128-note->num)/128.0f * 100;
        float noteEnd = note->end * 100;
        SDL_SetRenderDrawColor(renderer, colors.note[0],colors.note[1],colors.note[2],colors.note[3]);
        SDL_FRect noteRect = { noteX, noteY - 1, noteEnd - noteX, 2};
        SDL_RenderFillRect(renderer, &noteRect);
    }
    SDL_SetRenderTarget(renderer, NULL);

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

