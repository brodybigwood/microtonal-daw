#pragma once

#include <unordered_map>
#include "GridElement.h"

class ElementManager {
    public:
        static ElementManager* get();
        ~ElementManager();

        std::vector<GridElement*> elements;

        GridElement* getElement(uint16_t);
        void newRegion();
        void newAudioClip(std::string);

        uint16_t getIndex(uint16_t);
        idManager id_pool;
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
        bool hoverNew = false;

        int scrollY;

        bool handleInput(SDL_Event&);
};
