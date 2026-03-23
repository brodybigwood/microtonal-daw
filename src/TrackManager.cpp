#include "TrackManager.h"
#include <algorithm>
#include "WindowHandler.h"
#include <ranges>
#include "styles.h"
#include <SDL3_gfx/SDL3_gfxPrimitives.h>
#include "SongRoll.h"
#include "nodes/nodetypes.h"

TrackManager::TrackManager(ArrangerNode* n) : parentNode(n) {}


void TrackManager::setGeometry(SDL_FRect* dstRect, SDL_Renderer*& r) {

    this->dstRect = dstRect;

    newTrackRectE = SDL_FRect{
        dstRect->x, dstRect->y + dstRect->h - 20.0f,
        dstRect->w / 2.0f, 20.0f
    };

    newTrackRectW = SDL_FRect{
        dstRect->x + dstRect->w / 2.0f, dstRect->y + dstRect->h - 20.0f,
        dstRect->w / 2.0f, 20.0f
    };

    newTrackE = new Button(r);
    newTrackE->dstRect = &newTrackRectE;
    newTrackE->title = "New Track (notes)";

    newTrackE->activated = [] { return false; };
    newTrackE->hover = [this] { return this->mouseOn(&(this->newTrackRectE)); };
    newTrackE->onClick = [this] { this->addTrack(TrackType::Notes); };

    newTrackW = new Button(r);
    newTrackW->dstRect = &newTrackRectW;
    newTrackW->title = "New Track (audio)";

    newTrackW->activated = [] { return false; };
    newTrackW->hover = [this] { return this->mouseOn(&(this->newTrackRectW)); };
    newTrackW->onClick = [this] { this->addTrack(TrackType::Audio); };
}

TrackManager::~TrackManager() {

    delete newTrackE;
    delete newTrackW;

    for (auto t : tracks) {
        delete t;
    }
    tracks.clear();
}

void TrackManager::addTrack(TrackType tp) {
    uint16_t id = id_pool.newID();
    Track* t = new Track;
    tracks.push_back(t);
    t->id = id;
    ids[id] = tracks.size() - 1;
    t->type = tp;

    auto c = new Connection;
    
    c->dir = Direction::output;
    switch (tp) {
        case TrackType::Notes:
            c->type = DataType::Events;
            t->events = &(c->events);
            break;
        default:
            c->type = DataType::Waveform;
            t->buffer = &(c->buffer);
            break;
    }

    t->connection = c;
    parentNode->outputs.addConnection(c);
    parentNode->makeConnectionRects();
}

Track* TrackManager::getTrack(uint16_t id) {
    auto index = getIndex(id);
    if (index == -1) return nullptr;
    return tracks[index];
}

int TrackManager::getIndex(uint16_t id) {
    auto it = ids.find(id);
    if( it == ids.end()) return -1; 
    return it->second;
}

int TrackManager::getID(int index) {
    if (index < tracks.size()) return tracks[index]->id;
    else return -1;
}

void TrackManager::solo(uint16_t id) {
    auto it = std::find(soloTracks.begin(), soloTracks.end(), id);
    if (it != soloTracks.end()) {
        soloTracks.erase(it);
    } else {
        soloTracks.push_back(id);
    }
};

void TrackManager::mute(uint16_t id) {
    auto it = std::find(muteTracks.begin(), muteTracks.end(), id);
    if (it != muteTracks.end()) {
        muteTracks.erase(it);
    } else {
        muteTracks.push_back(id);
    }
}

bool TrackManager::mouseOn(SDL_FRect* rect) {
    int x = *mouseX;
    int y = *mouseY;
    return (
            x > rect->x &&
            x < rect->x + rect->w &&
            y > rect->y &&
            y < rect->y + rect->h
        );
}

void TrackManager::render(SDL_Renderer* renderer) {
    
    SDL_FRect trackRect{
        dstRect->x, dstRect->y - *scrollY, dstRect->w, *divHeight
    };

    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
    SDL_RenderFillRect(renderer, dstRect);

    for(auto track : tracks) {
        renderTrack(renderer, track, &trackRect);
        trackRect.y += *divHeight;
        if(trackRect.y >= dstRect->y + dstRect->h) break;
    }

    if (movingTrack) {
        size_t moveFromIDX = ids[movingTrack->id];
        int raw_new_idx = static_cast<int>(moveFromIDX) + moveAmount;
        size_t new_idx = std::max(0, std::min(raw_new_idx, static_cast<int>(tracks.size())));
        if (moveAmount > 0 && new_idx != tracks.size()) new_idx++;

        float y = new_idx * *divHeight - *scrollY + dstRect->y;
        thickLineRGBA(renderer, dstRect->x, y, dstRect->x + dstRect->w, y, 3, 0, 0, 255, 255);
    }

    newTrackE->render();
    newTrackW->render();
}

void TrackManager::handleTrackInput(Track* track, int y, SDL_Event& e) {
    SDL_FRect trackRect{
        dstRect->x, y + dstRect->y - *scrollY, dstRect->w, *divHeight
    };

    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            break;
        default:
            break;
    }
}

void TrackManager::moveTrack() {
    if (!movingTrack) return;

    auto connection = movingTrack->connection;

    auto it = std::find(tracks.begin(), tracks.end(), movingTrack);
    size_t idx = std::distance(tracks.begin(), it);

    tracks.erase(it);

    int raw_new_idx = static_cast<int>(idx) + moveAmount;
    size_t new_idx = std::max(0, std::min(raw_new_idx, static_cast<int>(tracks.size())));
    tracks.insert(tracks.begin() + new_idx, movingTrack);

    // that messed up the id map so rebuild it
    ids.clear();
    for (size_t i = 0; i < tracks.size(); ++i) ids[tracks[i]->id] = i;       

    movingTrack = nullptr;

    auto& connections = parentNode->outputs.connections;
    auto it1 = std::find(connections.begin(), connections.end(), connection);
    idx = std::distance(connections.begin(), it1);
    connections.erase(it1);

    raw_new_idx = static_cast<int>(idx) + moveAmount;
    new_idx = std::max(0, std::min(raw_new_idx, static_cast<int>(connections.size())));
    connections.insert(connections.begin() + new_idx, connection);

    // that messed up the id map so rebuild it
    parentNode->outputs.ids.clear();
    for (size_t i = 0; i < connections.size(); ++i) parentNode->outputs.ids[connections[i]->id] = i;

    parentNode->makeConnectionRects();
}

void TrackManager::handleInput(SDL_Event& e) {
    
    hoveredTrack = nullptr;

    int y = dstRect->y;
    for (auto track : tracks) {
        if (*mouseY >= y && *mouseY < y + *divHeight) {
            hoveredTrack = track;
            handleTrackInput(track, y, e);
            break;
        }
        y += *divHeight;
    }

    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if( newTrackE->hover()) newTrackE->onClick();
                else if( newTrackW->hover()) {
                    newTrackW->onClick();
                }
                movingTrack = hoveredTrack;
                last_lmb_y = *mouseY;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                moveTrack();
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            moveAmount = std::round((*mouseY - last_lmb_y) / *divHeight);
            break;
        default:
            break;
    };
}

void TrackManager::renderTrack(SDL_Renderer* renderer, Track* track, SDL_FRect* rect) {

    //body

    if (track == hoveredTrack) {
        SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    }
    SDL_RenderFillRect(renderer, rect);

    //type indicator
    SDL_FRect typeRect{
        rect->x, rect->y, rect->w / 10.0f, rect->h
    };
    switch(track->type) {
        case TrackType::Audio:
            SDL_SetRenderDrawColor(renderer, 255, 120, 120, 255);
            break;
        case TrackType::Automation:
            SDL_SetRenderDrawColor(renderer, 255, 220, 50, 255);
            break;
        case TrackType::Notes:
            SDL_SetRenderDrawColor(renderer, 120, 255, 120, 255);
            break;
        default:
            break;
    }
    SDL_RenderFillRect(renderer, &typeRect);

    // bus text - needs optimization

    std::string text = std::to_string(track->id);
    SDL_Color color{0, 0, 0, 255};    

    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text.c_str(), 0, color);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);

    SDL_RenderTexture(renderer, tex, nullptr, &typeRect);

    SDL_DestroySurface(surf);
    SDL_DestroyTexture(tex);

    //borders
    SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    SDL_RenderRect(renderer, &typeRect); //type
    SDL_RenderRect(renderer, rect); //outer
}

void TrackManager::process(float* input, int bufferSize) {
    for(auto track : tracks) {
        track->process(input, bufferSize);
    }
}

void TrackManager::fromJSON(json j) {
    soloTracks = j["soloTracks"].get<std::vector<uint16_t>>();
    muteTracks = j["muteTracks"].get<std::vector<uint16_t>>();

    for(auto t : j["tracks"] ) {
        Track* tr = new Track;
        tr->connection = parentNode->outputs.getConnection(t["connectionID"]);
        tr->buffer = &(tr->connection->buffer);
        tr->events = &(tr->connection->events);
        tr->fromJSON(t);
        tracks.push_back(tr);
    }

    id_pool.fromJSON(j["id_pool"]);

    for( auto [id, index] : j["ids"].items()) {
        ids[static_cast<uint16_t>(std::stoi(id))] = index.get<uint16_t>();
    }
}

json TrackManager::toJSON() {
    json j;
    j["soloTracks"] = soloTracks;
    j["muteTracks"] = muteTracks;

    j["tracks"] = json::array();
    for( auto t : tracks ){ 
        auto jt = t->toJSON();
        jt["connectionID"] = t->connection->id;
        j["tracks"].push_back(jt);
    };

    j["id_pool"] = id_pool.toJSON();

    j["ids"] = json::object();
    for( auto [id, index] : ids) {
        j["ids"][std::to_string(id)] = index;
    }

    return j;
}
