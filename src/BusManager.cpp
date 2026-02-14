#include "BusManager.h"
#include <cstring> 

BusManager* BusManager::get() {
    static BusManager bm;
    return &bm;
}

BusManager::BusManager() {

    for (size_t i = 0; i < busCount; ++i) {
        auto& bus = eventBus[i];
        bus.id = i;
        bus.output.data = &bus;
    }

    for (size_t i = 0; i < busCount; ++i) {
        auto& bus = waveformBus[i];
        bus.id = i;
        bus.output.data = &bus;
    }
}

void BusManager::process(int bufferSize) {

    if (bufferSize != this->bufferSize) {
        this->bufferSize = bufferSize;
    
        for (size_t i = 0; i < busCount; ++i) {
            auto& bus = waveformBus[i];
            bus.bufferSize = bufferSize;
            if (bus.buffer) delete[] bus.buffer;
            bus.buffer = new float[bufferSize];
        }
    }

    for (size_t i = 0; i < busCount; ++i) {
        auto& bus = waveformBus[i];
        std::memset(bus.buffer, 0, bufferSize * sizeof(float));
    }
}

BusManager::~BusManager() {

    for(size_t i = 0; i < busCount; ++i) {
        delete[] waveformBus[i].buffer;
    }
}

EventBus* BusManager::getBusE(size_t index) {
    if (index >= busCount) return nullptr;
    return &(eventBus[index]);
}

WaveformBus* BusManager::getBusW(size_t index) {
    if (index >= busCount) return nullptr;
    return &(waveformBus[index]);
}
