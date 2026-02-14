#pragma once

#include "Bus.h"
#include <vector>
#include <cstddef>

#define BUSCOUNT 256

class BusManager {

    public:
        BusManager();
        ~BusManager();

        static BusManager* get();
    
        size_t busCount = BUSCOUNT;
        
        EventBus* getBusE(size_t);
        WaveformBus* getBusW(size_t);

        void process(int bufferSize);
        int bufferSize;

    private:
        EventBus eventBus[BUSCOUNT];
        WaveformBus waveformBus[BUSCOUNT];
};
