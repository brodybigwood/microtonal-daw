#pragma once

#include <unordered_map>
#include "GridElement.h"

class Region;
class AudioClip;
class AutomationCurve;
class Project;
class TrackManager;
class ArrangerNode;

class ElementManager {
    public:
        ElementManager(Project*, TrackManager*, ArrangerNode*);
        ~ElementManager();

        void clearTextures();
        std::vector<GridElement*> elements;

        GridElement* getElement(uint16_t);
        Region* newRegion();
        AudioClip* newAudioClip(std::string);
        AutomationCurve* newAutomationCurve();

        void removeElementById(uint16_t elementId);
        void restoreRegionFromSnapshot(const json& regionJson);
        void restoreRegionFromSnapshotAt(size_t insertIndex, const json& regionJson);
        void restoreAutomationCurveFromSnapshot(const json& curveJson);
        void restoreAutomationCurveFromSnapshotAt(size_t insertIndex, const json& curveJson);
        void restoreAudioClipFromSnapshot(const json& clipJson);
        void restoreAudioClipFromSnapshotAt(size_t insertIndex, const json& clipJson);

        uint16_t getIndex(uint16_t);
        idManager id_pool;
        idManager position_id_pool;
        std::unordered_map<uint16_t, uint16_t> ids;

        json toJSON();
        void fromJSON(json);

        void process(int bufferSize);

        int currentElement = -1;
        int hoveredElement = -1;

        SDL_FRect* dstRect;
        
        void render(SDL_Renderer*);

        float mouseX;
        float mouseY;
        bool hoverNewRegion = false;
        bool hoverNewWaveform = false;

        int scrollY;

        bool handleInput(SDL_Event&);

        Project* project;
        TrackManager* tm;

        ArrangerNode* parentNode;
};
