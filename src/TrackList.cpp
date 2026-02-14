#include "TrackList.h"
#include <algorithm>
#include "WindowHandler.h"
#include <ranges>
#include "BusManager.h"
#include "styles.h"

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

    delete newTrackE;
    delete newTrackW;

    for (auto t : tracks) {
        delete t;
    }
    tracks.clear();
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
    if (index == -1) return nullptr;
    return tracks[index];
}

int TrackList::getIndex(uint16_t id) {
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

void TrackList::render(SDL_Renderer* renderer) {
    
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

    newTrackE->render();
    newTrackW->render();
}

void TrackList::handleTrackInput(Track* track, int y, SDL_Event& e) {
    SDL_FRect trackRect{
        dstRect->x, y + dstRect->y - *scrollY, dstRect->w, *divHeight
    };

    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_RIGHT) {
                auto* ctxMenu = ContextMenu::get();
                ctxMenu->active = true;

                auto window = Home::get()->song->window;
                ctxMenu->window_id = SDL_GetWindowID(window);
                ctxMenu->renderer = Home::get()->song->renderer;
                SDL_StartTextInput(window);

                ctxMenu->locX = *mouseX;
                ctxMenu->locY = *mouseY;
    
                ctxMenu->dynamicTick = getTextInputTicker([this,track](std::string text)
{
    try {
        int index = std::stoi(text);
        
        auto bm = BusManager::get();
        if (index >= bm->busCount) throw std::out_of_range("bus index out of range");
        assign(track, index);
    } catch(...) {
    std::cerr << "invalid input: " << text << std::endl;
    }
}
                );
            }
            break;
        default:
            break;
    }
}

void TrackList::handleInput(SDL_Event& e) {
    
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
            }

            break;
        default:
            break;
    };
}

void TrackList::renderTrack(SDL_Renderer* renderer, Track* track, SDL_FRect* rect) {

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

    std::string text = std::to_string(track->busID);
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

TrackList* TrackList::get() {
    static TrackList t;
    return &t;
}

void TrackList::process(float* input, int bufferSize) {
    for(auto track : tracks) {
        track->process(input, bufferSize);
    }
}

void TrackList::fromJSON(json j) {
    soloTracks = j["soloTracks"].get<std::vector<uint16_t>>();
    muteTracks = j["muteTracks"].get<std::vector<uint16_t>>();

    for(auto t : j["tracks"] ) {
        Track* tr = new Track;
        tr->fromJSON(t);
        tracks.push_back(tr);
    }

    id_pool.fromJSON(j["id_pool"]);

    for( auto [id, index] : j["ids"].items()) {
        ids[static_cast<uint16_t>(std::stoi(id))] = index.get<uint16_t>();
    }

    assignedBusses.event = j["assignedBusses"]["event"].get<std::vector<int>>();
    assignedBusses.waveform = j["assignedBusses"]["waveform"].get<std::vector<int>>();
}

json TrackList::toJSON() {
    json j;
    j["soloTracks"] = soloTracks;
    j["muteTracks"] = muteTracks;

    j["tracks"] = json::array();
    for( auto t : tracks ){ 
        j["tracks"].push_back(t->toJSON());
    };

    j["id_pool"] = id_pool.toJSON();

    j["ids"] = json::object();
    for( auto [id, index] : ids) {
        j["ids"][std::to_string(id)] = index;
    }

    json ab;
    ab["event"] = assignedBusses.event;
    ab["waveform"] = assignedBusses.waveform;

    j["assignedBusses"] = ab;
    return j;
}

void TrackList::assign(Track* track, int busID) {
    BusManager* bm = BusManager::get();

    bool success = false;

    switch(track->type) {
        case TrackType::Notes:
            if (std::ranges::find(assignedBusses.event, busID) == assignedBusses.event.end())
            {
                std::erase(assignedBusses.event, track->busID);

                track->dstBus = bm->getBusE(busID);
                track->events = bm->getBusE(busID)->events;
                assignedBusses.event.push_back(busID);
                success = true;
            }
            break;
        default:
            if (std::ranges::find(assignedBusses.waveform, busID) == assignedBusses.waveform.end())
            {
                std::erase(assignedBusses.waveform, track->busID);            

                track->dstBus = bm->getBusW(busID);

                auto* b = static_cast<WaveformBus*>(track->dstBus);
                track->buffer = b->buffer;

                assignedBusses.waveform.push_back(busID);
                success = true;
            }
            break;
    }

    if (success) track->busID = track->dstBus->id;

}
