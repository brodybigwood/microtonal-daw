#include "BusManager.h"

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
