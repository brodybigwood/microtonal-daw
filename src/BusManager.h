#pragma once

#include "Bus.h"
#include <vector>
#include <cstddef>

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
        EventBus* eventBus;
        WaveformBus* waveformBus;

        size_t eventBusCount = 256;
        size_t waveformBusCount = 256;
};
