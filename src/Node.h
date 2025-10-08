#pragma once
#include <vector>
#include <string>
#include "Bus.h"
#include "idManager.h"
#include <unordered_map>

struct sourceNode{
    ConnectionType type;
    uint16_t source_id;
    uint16_t output_id;
};

struct connectionSet{
    std::vector<Connection*> connections;
    uint8_t num_outputs;
    idManager id_pool;

    std::unordered_map<uint16_t,uint16_t> ids;

    uint16_t getIndex(uint16_t id);
    Connection* getConnection(uint16_t id);
};

class Node {
    public:
        Node(uint16_t);
        ~Node();

        uint16_t id;
        std::string name;

        connectionSet inputs;

        connectionSet outputs;

        void* getOutput(uint16_t);

        void* getInput(uint16_t);

        EventBus* getEvents(void*);
        WaveformBus* getWaveform(void*);

        virtual void process() = 0;
}; 
