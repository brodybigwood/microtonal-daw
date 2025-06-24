#include "Rack.h"
#include "EventList.h"
#include "PluginManager.h"

Rack::Rack() {
}

Rack::~Rack() {

}

void Rack::process(
    float* tempBuffer,
    float* outputBuffer,
    int bufferSize,
    EventList* eventList
) {

    size_t pluginCount = plugins.size();
 
    for(size_t i = 0; i<pluginCount; i++) {
        plugins[i]->process(tempBuffer, bufferSize, eventList);
    }

    for (size_t i = 0; i < bufferSize; i++) {
        outputBuffer[i] += tempBuffer[i];
    }

}

bool Rack::addPlugin(char* filepath) {
    Plugin* p = new Plugin(filepath);
    plugins.push_back(p);
    PluginManager::instance().addPlugin(p);
    return true;
}

