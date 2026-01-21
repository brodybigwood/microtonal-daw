#pragma once

#include "Track.h"
#include "Button.h"
#include <unordered_map>
#include "idManager.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class TrackList {
    public:

        TrackList();
        ~TrackList();
       
        static TrackList* get();

        void addTrack(TrackType);

        void solo(uint16_t);
        void mute(uint16_t);

        void render(SDL_Renderer*, int, float);

        void handleInput(SDL_Event&);

        void process(float* input, int bufferSize);

        void setGeometry(SDL_FRect*);

        SDL_FRect* dstRect;

        bool mouseOn(SDL_FRect*);

        float* mouseX;
        float* mouseY;

        uint8_t getBus(uint16_t);

        Track* getTrack(uint16_t);

        int getIndex(uint16_t);

        void fromJSON(json);
        json toJSON();

    private:
        std::vector<uint16_t> soloTracks;
        std::vector<uint16_t> muteTracks;

        Button* newTrackE;
        Button* newTrackW;

        SDL_FRect newTrackRectE;
        SDL_FRect newTrackRectW;

        std::vector<Track*> tracks;
        idManager id_pool;

        std::unordered_map<uint16_t, uint16_t> ids;
};
