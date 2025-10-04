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

        EventBus* getBusE(size_t);
        WaveformBus* getBusW(size_t);

    private:
        EventBus* eventBus;
        WaveformBus* waveformBus;

        size_t eventBusCount = 1024;
        size_t waveformBusCount = 1024;
};
