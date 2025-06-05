#pragma once
#include <vector>
#include "Plugin.h";

class Plugin;  // forward declaration

class PluginManager {
    std::vector<Plugin*> plugins;
public:
    void addPlugin(Plugin* p);
    void tickAll();
    static PluginManager& instance();
};
