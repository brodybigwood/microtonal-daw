#include "Plugin.h"
#include <vector>
#include <iostream>
#include <memory>
#include "EventList.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include "Processing.h"
#include "PlugFormat.h"

#ifndef RACK_H
#define RACK_H

class Plugin;

class Rack {
    public:
    Rack();
    ~Rack();

    std::vector<Plugin*> plugins;

    enum RackTypes {
        parallel,
        series,
    };

    void process(
        audioData data,
        EventList* eventList = nullptr,
        RackTypes rackType = parallel
    );
  
    bool addPlugin(char* filepath, PlugFormat pf);

    json toJSON();
    void fromJSON(json);
    uint16_t id_plug = 0;
    uint16_t id;

    int getIndex(Plugin*);

};
#endif
