#include "TrackList.h"
#include <algorithm>
#include "WindowHandler.h"

TrackList::TrackList() {}


void TrackList::setGeometry(SDL_FRect* dstRect) {

    auto wm = WindowHandler::instance();
    auto r = wm->renderer;

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

TrackList::~TrackList() {

}

void TrackList::addTrack(TrackType tp) {
    uint16_t id = id_pool.newID();
    Track* t = new Track;
    tracks.push_back(t);
    t->id = id;
    ids[id] = tracks.size() - 1;
    t->type = tp;
}

Track* TrackList::getTrack(uint16_t id) {
    auto index = getIndex(id);
    return tracks[index];
}

uint16_t TrackList::getIndex(uint16_t id) {
    auto it = ids.find(id);
    if( it == ids.end()) return -1; 
    return it->second;
};

void TrackList::solo(uint16_t id) {
    auto it = std::find(soloTracks.begin(), soloTracks.end(), id);
    if (it != soloTracks.end()) {
        soloTracks.erase(it);
    } else {
        soloTracks.push_back(id);
    }
};

void TrackList::mute(uint16_t id) {
    auto it = std::find(muteTracks.begin(), muteTracks.end(), id);
    if (it != muteTracks.end()) {
        muteTracks.erase(it);
    } else {
        muteTracks.push_back(id);
    }
}

bool TrackList::mouseOn(SDL_FRect* rect) {
    int x = *mouseX;
    int y = *mouseY;
    return (
            x > rect->x &&
            x < rect->x + rect->w &&
            y > rect->y &&
            y < rect->y + rect->h
        );
}

void TrackList::render(SDL_Renderer* renderer, int scrollY, float h) {
    SDL_FRect trackRect{
        dstRect->x, dstRect->y - scrollY, dstRect->w, h
    };


    SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
    SDL_RenderFillRect(renderer, dstRect);

    for(auto track : tracks) {
        track->render(renderer, trackRect);
        trackRect.y += h;
        if(trackRect.y >= dstRect->y + dstRect->h) break;
    }

    newTrackE->render();
    newTrackW->render();
}

void TrackList::handleInput(SDL_Event& e) {
    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if( newTrackE->hover()) newTrackE->onClick();
                else if( newTrackW->hover()) newTrackW->onClick();
            }
            break;
        default:
            break;
    };
}

TrackList* TrackList::get() {
    static TrackList t;
    return &t;
}

void TrackList::process(float* input, int bufferSize) {
    for(auto track : tracks) {
        track->process(input, bufferSize);
    }
}
