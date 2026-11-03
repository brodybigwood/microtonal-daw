#pragma once

#include "Track.h"
#include "Button.h"
#include <unordered_map>
#include "idManager.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class ElementManager;
class SongRoll;
class ArrangerNode;
class Project;

class TrackManager {
    friend class ElementManager;
    public:

        TrackManager(ArrangerNode*);
        ~TrackManager();
       
        SongRoll* songRoll;
        ArrangerNode* parentNode;

        void addTrack(TrackType);
        Track* addTrackNow(TrackType, int forcedTrackID = -1, int forcedConnectionID = -1, int forcedIndex = -1,
                           int forcedInputConnectionID = -1);
        void removeTrackNow(uint16_t);

        // Per-track input ports (Events for Notes tracks, Waveform for Audio tracks), kept in
        // track order on the arranger's inputs.
        Connection* addInputConnection(TrackType, int forcedID = -1, int insertIndex = -1);
        void removeInputConnectionForTrack(Track*);
        void syncInputPortOrder();

        bool isSoloed(uint16_t id) const;
        bool isMuted(uint16_t id) const;
        bool isAudible(uint16_t id) const;
        bool anySoloed() const { return !soloTracks.empty(); }
        void setSoloState(uint16_t id, bool on);
        void setMuteState(uint16_t id, bool on);
        void setRecordArmedState(uint16_t id, bool on);
        void setMonitorState(uint16_t id, bool on);

        std::vector<Track*>& getTracks() { return tracks; }
        const std::vector<Track*>& getTracks() const { return tracks; }

        void processAfterElements(int bufferSize);

        // Recording (DSP thread): capture armed tracks' inputs per block and hand finished
        // takes to the project's queue.
        void captureTakes(Project* project, int bufferSize);
        void finalizeTake(Project* project, Track* track);
        uint64_t lastSeenTransportGeneration = 0;

        void solo(uint16_t);
        void mute(uint16_t);

        void render(SDL_Renderer*);
        void renderTrack(SDL_Renderer*, Track*, SDL_FRect*);

        void handleInput(SDL_Event&);
        bool handleTrackInput(Track*, int, SDL_Event& e);

        void process(float* input, int bufferSize);

        void setGeometry(SDL_FRect*, SDL_Renderer*&);

        SDL_FRect* dstRect;

        bool mouseOn(SDL_FRect*);

        float* mouseX;
        float* mouseY;
        int* scrollY;
        float* divHeight = nullptr;

        uint8_t getBus(uint16_t);

        Track* getTrack(uint16_t);
        Track* hoveredTrack = nullptr;

        int moveAmount;
        Track* movingTrack = nullptr;
        float last_lmb_y;
        void moveTrack();

        int getIndex(uint16_t);
        int getID(int);
        idManager& getIdPool() { return id_pool; }

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
