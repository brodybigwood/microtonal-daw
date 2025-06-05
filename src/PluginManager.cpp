#include "PluginManager.h"

void PluginManager::addPlugin(Plugin* p) {
    plugins.push_back(p);
}

void PluginManager::tickAll() {
    for (auto& p : plugins)
        p->editorTick();
}

PluginManager& PluginManager::instance() {
    static PluginManager mgr;
    return mgr;
}
