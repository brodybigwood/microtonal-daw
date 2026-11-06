#pragma once

#include <unordered_map>
#include <cstdint>
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

        /** Rebuild the element order from a list of element ids (undo/reorder). */
        void applyElementOrder(const std::vector<uint16_t>& order);
        /** Restore an element snapshot of any type at a list index (undo delete). */
        void restoreElementFromSnapshotAt(size_t insertIndex, const json& snapshot);

        json toJSON();
        void fromJSON(json);

        void process(int bufferSize);
        uint64_t lastTransportGeneration = 0;

        int currentElement = -1;
        int hoveredElement = -1;

        SDL_FRect* dstRect;

        void render(SDL_Renderer*);
        /** Element-list row index under a point, or -1. */
        int rowIndexAt(float x, float y);

        float mouseX;
        float mouseY;
        bool hoverNewRegion = false;
        bool hoverNewAutomation = false;

        int scrollY = 0;

        // Drag-to-reorder state.
        bool dragPending = false;
        bool dragActive = false;
        uint16_t draggedElementID = 0;
        float dragStartY = 0.0f;
        int dropIndex = -1;
        bool isDraggingElement() const { return dragActive; }

        bool handleInput(SDL_Event&);

        Project* project;
        TrackManager* tm;

        ArrangerNode* parentNode;
};
