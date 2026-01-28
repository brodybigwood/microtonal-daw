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

        void render(SDL_Renderer*);
        void renderTrack(SDL_Renderer*, Track*, SDL_FRect*);

        void handleInput(SDL_Event&);
        void handleTrackInput(Track*, int, SDL_Event& e);

        void process(float* input, int bufferSize);

        void setGeometry(SDL_FRect*);

        SDL_FRect* dstRect;

        bool mouseOn(SDL_FRect*);

        float* mouseX;
        float* mouseY;
        int* scrollY;
        float* divHeight = nullptr;

        uint8_t getBus(uint16_t);

        Track* getTrack(uint16_t);
        Track* hoveredTrack = nullptr;

        int getIndex(uint16_t);

        void fromJSON(json);
        json toJSON();

        void assign(Track*, int);

        struct busList {
            std::vector<int> event;
            std::vector<int> waveform;
        };
        
        busList assignedBusses;

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
