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

Region::Region(Project* p, ScaleManager* sm, ArrangerNode* n) : GridElement(p, n), sm(sm) {
    scale = sm->getLastScale();
    type = ElementType::region;
}

Region::~Region() {

}

void Region::draw(SDL_Renderer* renderer, float pixelsPerSecond, int h) {

    if(!texture) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 10000, 100);
    }
   
    auto target = SDL_GetRenderTarget(renderer); 
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer,0,0,0,0);
    SDL_RenderClear(renderer);

    for(std::shared_ptr<Note> note : notes) {
        float noteX = (float)note->start * 100;
        float noteY = (128-note->num)/128.0f * 100;
        float noteEnd = (float)note->end * 100;
        SDL_SetRenderDrawColor(renderer, colors.note[0],colors.note[1],colors.note[2],colors.note[3]);
        SDL_FRect noteRect = { noteX, noteY - 1, noteEnd - noteX, 2};
        SDL_RenderFillRect(renderer, &noteRect);
    }
    SDL_SetRenderTarget(renderer, target);

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
    j["idManager"] = id_pool.toJSON();

    return j;
}

void Region::fromJSON(json j) {
    name = j["name"];
    releaseMS = j["releaseMS"];
    GridElement::fromJSON(j["positions"]);
    id = j["id"];
    type = j["type"];
    for (auto e : j["notes"]) {
        notes.push_back(Note::fromJSON(e, sm));
        id_to_index[e["id"]] = notes.size() - 1;
    }
    id_pool.fromJSON(j["idManager"]);
}

int Region::createNote(fract start, fract length, float pitch, TuningTable* t) {
    auto n = std::make_shared<Note>(start, start + length, pitch, sm);
    n->scale = t;
    notes.push_back(n);
    n->id = id_pool.newID();
    id_to_index[n->id] = notes.size() - 1;
    return n->id;
}

void Region::deleteNote(int id) {

    auto idx = id_to_index[id];
    
    notes.erase(notes.begin() + idx);
    id_to_index.erase(id);

    for (auto& [k, v] : id_to_index) {
        if (v > idx)
            --v;
    }

    id_pool.releaseID(id);
}
