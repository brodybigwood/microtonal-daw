#pragma once

#include "Track.h"
#include "Button.h"

class TrackList {
    public:

        TrackList();
        ~TrackList();
       
        static TrackList* get();

        void addTrack(TrackType);

        std::vector<Track*> tracks;

        void solo(uint16_t);
        void mute(uint16_t);

        void render(SDL_Renderer*, int, float);

        void handleInput(SDL_Event&);

        void process(float* input, float* output, int bufferSize);

        void setGeometry(SDL_FRect*);

        SDL_FRect* dstRect;

        bool mouseOn(SDL_FRect*);

        float* mouseX;
        float* mouseY;

    private:
        std::vector<uint16_t> soloTracks;
        std::vector<uint16_t> muteTracks;

        Button* newTrackE;
        Button* newTrackW;

        SDL_FRect newTrackRectE;
        SDL_FRect newTrackRectW;
};
