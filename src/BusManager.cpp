#include "BusManager.h"

BusManager* BusManager::get() {
    static BusManager bm;
    return &bm;
}

BusManager::BusManager() {
    eventBus = new EventBus[eventBusCount];
    waveformBus = new WaveformBus[waveformBusCount];
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
