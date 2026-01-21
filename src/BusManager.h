#pragma once

#include "Bus.h"
#include <vector>
#include <cstddef>

#define BUSCOUNT_E 256
#define BUSCOUNT_W 256

class BusManager {

    public:
        BusManager();
        ~BusManager();

        static BusManager* get();

        size_t getBusCountE();
        size_t getBusCountW();

        EventBus* getBusE(uint8_t);
        WaveformBus* getBusW(uint8_t);

    private:
        EventBus eventBus[BUSCOUNT_E];
        WaveformBus waveformBus[BUSCOUNT_W];

        size_t eventBusCount = BUSCOUNT_E;
        size_t waveformBusCount = BUSCOUNT_W;
};
