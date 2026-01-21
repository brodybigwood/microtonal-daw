#pragma once
#include <vector>
#include <string>
#include "Bus.h"
#include "idManager.h"
#include <unordered_map>
#include <SDL3/SDL.h>

struct sourceNode{
    ConnectionType type;
    uint16_t source_id;
    uint16_t output_id;
};

struct connectionSet{
    std::vector<Connection*> connections;
    idManager id_pool;

    std::unordered_map<uint16_t,uint16_t> ids;

    uint16_t getIndex(uint16_t id);
    Connection* getConnection(uint16_t id);

    void addConnection(Connection*);
};

class Node {
    public:
        Node(uint16_t);
        ~Node();

        uint16_t id;
        std::string name;

        connectionSet inputs;

        connectionSet outputs;

        void* getOutput(Connection*);
        void* getInput(Connection*);

        EventBus* getEvents(void*);
        WaveformBus* getWaveform(void*);

        virtual void process() = 0;

        SDL_FRect dstRect{0,0,50,50};
        void move(float& x, float& y);

        void render(SDL_Renderer*);

        int bufferSize;
        int sampleRate;

        virtual void setup();

        void update(int, int);

        float mouseX;
        float mouseY;

        int hoveredConnection;
        Direction hoveredDirection;

        void handleInput(SDL_Event&);

       void renderConnection(SDL_Renderer*, uint16_t);

}; 
