
#ifndef PLUGIN_H
#define PLUGIN_H

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include <string>

class Plugin {
    public:
        Plugin(const char* filepath);
        ~Plugin();

         void load();
         
         void process(
            float* thrubuffer,
            int bufferSize
        );

        const char* filepath;

        void* pluginLibraryHandle;

        Steinberg::IPluginFactory* pluginFactory;

        std::string vendor;
        std::string url;
        std::string email;
        int32_t flags;

};

#endif