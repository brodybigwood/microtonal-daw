#pragma once
#include <vector>
#include "Plugin.h"
#include "PlugLib.h"
#include <unordered_map>
#include <string>
#include <iostream>

class Plugin;  // forward declaration

class PluginManager {
    std::vector<Plugin*> instances;
public:
    void addPlugin(Plugin* p);
    void tickAll();

    static PluginManager& instance();
    std::unordered_map<std::string, std::unique_ptr<PlugLib>> loaded;
    PlugLib* load(const char* path);
};
