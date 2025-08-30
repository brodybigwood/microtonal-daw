#include "PluginManager.h"
#include "VstLib.h"
#include "WavLib.h"

void PluginManager::addPlugin(Plugin* p) {
    instances.push_back(p);
}

void PluginManager::tickAll() {
    for (auto& p : instances)
        p->editorTick();
}

PlugLib* PluginManager::load(const char* path, PlugFormat p) {
    auto it = loaded.find(path);
    if (it != loaded.end()) {
        std::cout << "already loaded" <<std::endl;
        return it->second.get();
    }
    // Not loaded yet — create and insert
    
    switch(p) {
	    case PlugFormat::VST: {

		    auto plugLib = std::make_unique<VstLib>(std::string(path));
		    PlugLib* ptr = plugLib.get();
		    loaded.emplace(path, std::move(plugLib));
		    if(!ptr) std::cout << "ptr is null" << std::endl;
		    if(!ptr->load()) {
		        std::cout << "loading failed" << std::endl;
		        loaded.erase(path);
		        return nullptr;
		    }
		    return ptr;
            }
	    case PlugFormat::Wavnode: {
		    auto plugLib = std::make_unique<WavLib>(std::string(path));
		    PlugLib* ptr = plugLib.get();
		    loaded.emplace(path, std::move(plugLib));
		    return ptr;
	    }
    }
}


PluginManager& PluginManager::instance() {
    static PluginManager mgr;
    return mgr;
}
