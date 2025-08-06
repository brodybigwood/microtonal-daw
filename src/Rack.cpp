#include "Rack.h"
#include "EventList.h"
#include "EventManager.h"
#include "PluginManager.h"
#include "Project.h"

Rack::Rack() {
    id = (Project::instance()->id_rack)++;
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
    p->id = id_plug++;
    PluginManager::instance().addPlugin(p);
    return true;
}

json Rack::toJSON() {
    json j;
    json arr = json::array();
    size_t i = 0;
    for (auto plugin : plugins) {
        json plug;
        plug = plugin->toJSON();
        plug["index"] = i++;
        arr.push_back(plug);
    }
    j["plugins"] = arr;
    return j;
}

void Rack::fromJSON(json j) {
    constexpr uint16_t NO_ID = std::numeric_limits<uint16_t>::max();

    if (j.contains("plugins")) {
        std::cout<<"checking for plugins..."<<std::endl;
        for (auto& jt : j["plugins"]) {
            id_plug = jt.value("id", NO_ID);
            std::string spath = jt["path"];
            char* path = spath.data();
            std::cout<<path<<std::endl;
            Plugin* p = new Plugin(path);
            plugins.push_back(p);
            p->id = id_plug++;
            PluginManager::instance().addPlugin(p);
        }
    }

    uint16_t max_id = 0;
    for (auto plug : plugins) {
        if (plug->id > max_id) max_id = plug->id;
    }
    id_plug = max_id + 1;
}
