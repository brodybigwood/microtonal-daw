#include "Plugin.h"
#include <vector>
#include <iostream>
#include <memory>


#ifndef RACK_H
#define RACK_H

class Plugin;

class Rack {
    public:
    Rack();
    ~Rack();

    std::vector<Plugin*> plugins;

    void process(
        float* tempBuffer,
        float* outputBuffer,
        int bufferSize
    );
  
    bool addPlugin(char* filepath);
};
#endif
