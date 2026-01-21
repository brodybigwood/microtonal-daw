#include "BusManager.h"

BusManager* BusManager::get() {
    static BusManager bm;
    return &bm;
}

BusManager::BusManager() {

    for (size_t i = 0; i < eventBusCount; ++i) {
        auto& bus = eventBus[i];
        bus.id = i;
        bus.output.data = &bus;
    }

    for (size_t i = 0; i < waveformBusCount; ++i) {
        auto& bus = waveformBus[i];
        bus.id = i;
        bus.output.data = &bus;
    }
}

BusManager::~BusManager() {

    delete[] eventBus;

    for(size_t i = 0; i < waveformBusCount; ++i) {
        delete[] waveformBus[i].buffer;
    }

    delete[] waveformBus;

}

size_t BusManager::getBusCountE() {
    return eventBusCount;
}

size_t BusManager::getBusCountW() {
    return waveformBusCount;
}

EventBus* BusManager::getBusE(uint8_t index) {
    return &(eventBus[index]);
}

WaveformBus* BusManager::getBusW(uint8_t index) {
    return &(waveformBus[index]);
}
