#include "Rack.h"


Rack::Rack() {
    addPlugin();
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

bool Rack::addPlugin() {
    plugins.push_back(new Plugin);
    return true;
}