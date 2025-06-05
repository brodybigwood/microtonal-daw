#include "Rack.h"
#include "PluginManager.h"

Rack::Rack() {
}

Rack::~Rack() {

}

void Rack::process(
    float* tempBuffer,
    float* outputBuffer,
    int bufferSize
) {

    size_t pluginCount = plugins.size();
    std::cout << "Number of plugins: " << pluginCount << std::endl;
 
    for(size_t i = 0; i<pluginCount; i++) {
        plugins[i]->process(tempBuffer, bufferSize);
    }

    for (int i = 0; i < bufferSize; i++) {
        outputBuffer[i] += tempBuffer[i];
    }

}

bool Rack::addPlugin(char* filepath) {
    Plugin* p = new Plugin(filepath);
    plugins.push_back(p);
    PluginManager::instance().addPlugin(p);
    return true;
}

