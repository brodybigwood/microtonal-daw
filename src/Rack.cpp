#include "Rack.h"
#include "EventList.h"
#include "EventManager.h"
#include "PluginManager.h"
#include "Project.h"

Rack::Rack() {
    id = (Project::instance()->id_rack)++;
}

Rack::~Rack() {
    for (auto plug : plugins) {
        delete plug;
    }
    plugins.clear();
}

void Rack::process(
    audioData data,
    EventList* eventList,
    RackTypes rackType
) {

    switch(rackType) {
        case series: {
            audioData tempData {
                data.output,
                data.output,
                data.bufferSize
            };

            for(size_t i = 0; i< plugins.size(); i++) {
                plugins[i]->process(tempData, eventList);
            }
            break;
        }

        case parallel:
            for(size_t i = 0; i< plugins.size(); i++) {

                channel* tempChannels = new channel[data.output.numChannels];

                for (size_t i = 0; i < data.output.numChannels; i++) {
                    tempChannels[i].buffer = new float[data.bufferSize]();
                }

                audioStream output {
                    tempChannels,
                    data.output.numChannels
                };

                audioData tempData {
                    data.input,
                    output,
                    data.bufferSize
                };

                plugins[i]->process(tempData, eventList);

                for (size_t j = 0; j < tempData.output.numChannels; j++) {
                    float* dest = data.output.channels[j].buffer;
                    float* src = tempData.output.channels[j].buffer;
                    for (size_t i = 0; i < data.bufferSize; ++i) {
                        dest[i] += src[i];
                    }
                }

                for (size_t i = 0; i < data.output.numChannels; ++i) {
                    delete[] tempChannels[i].buffer;
                }
                delete[] tempChannels;
            }
            break;
    }

}

bool Rack::addPlugin(char* filepath) {
    Plugin* p = new Plugin(filepath);
    p->rack = this;
    plugins.push_back(p);
    p->id = id_plug++;
    PluginManager::instance().addPlugin(p);
    return true;
}

int Rack::getIndex(Plugin* plugin) {
    for (size_t i = 0; i < plugins.size(); ++i) {
        if (plugins[i] == plugin)
            return static_cast<int>(i);
    }
    return -1; // not found
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

        size_t pluginCount = j["plugins"].size();
        plugins.reserve(pluginCount);

        for (auto& jt : j["plugins"]) {
            id_plug = jt.value("id", NO_ID);
            std::string spath = jt["path"];
            char* path = spath.data();
            std::cout<<path<<std::endl;
            Plugin* p = new Plugin(path);
            p->rack = this;
            plugins.push_back(p);
            p->id = id_plug++;
            p->fromJSON(jt);
            PluginManager::instance().addPlugin(p);
        }
    }

    uint16_t max_id = 0;
    for (auto plug : plugins) {
        if (plug->id > max_id) max_id = plug->id;
    }
    id_plug = max_id + 1;
}
