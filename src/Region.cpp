#include "Region.h"
#include <set>
#include <functional>
#include "Project.h"
#include "fract.h"
#include "styles.h"
#include "ScaleManager.h"
#include "ElementManager.h"

TuningTable* Region::getTuning() {
    return scale;
}

Region::Region() {
    scale = ScaleManager::instance()->getLastScale();
    type = ElementType::region;
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

json Region::toJSON() {
    json j;
    j["name"] = name;
    j["releaseMS"] = releaseMS;
    j["id"] = id;
    j["type"] = ElementType::region;
    j["notes"] = json::array();
    for(auto e : notes) {
        j["notes"].push_back(e->toJSON());
    }
    j["positions"] = GridElement::toJSON();

    return j;
}

void Region::fromJSON(json j) {
    name = j["name"];
    releaseMS = j["releaseMS"];
    GridElement::fromJSON(j["positions"]);
    id = j["id"];
    type = j["type"];
    for (auto e : j["notes"]) {
        notes.push_back(Note::fromJSON(e));
    }
}
