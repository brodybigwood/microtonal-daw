#pragma once

#include "Node.h"
#include "SongRoll.h"

class TrackManager;
class ElementManager;

class ArrangerNode : public Node {
    public:
        ArrangerNode(uint16_t, NodeManager*);
        ~ArrangerNode() override;
        void process() override;
        void setup() override;
        bool handleCustomInput(SDL_Event&) override;
        void renderContent(SDL_Renderer*) override;

        void renderPresent() override;
        void clearCustomTextures() override;

        SDL_FRect* slRect;
        bool slDetached = false;
        SongRoll* sl = nullptr;

        json extraSerialize() override;
        void extraDeSerialize(json) override;

        /** SongRoll UI when open; otherwise the runtime managers used for audio/offline edits. */
        TrackManager* activeTrackManager();
        ElementManager* activeElementManager();

    private:
        void rebuildRuntimeState(json);
        void ensureSongRoll();
        void syncSongRollContext();
        json pendingExtraState;
        bool hasPendingExtraState = false;
        TrackManager* runtimeTracks = nullptr;
        ElementManager* runtimeElements = nullptr;
};
