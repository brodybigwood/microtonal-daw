
#ifndef PLUGIN_H
#define PLUGIN_H


#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"


#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"


#include <string>


class Plugin {
    public:
        Plugin(const char* filepath);
        ~Plugin();



         bool instantiate();
         
         void process(
            float* thrubuffer,
            int bufferSize
        );

        Steinberg::TUID componentCID;

        const char* filepath;

        void* pluginLibraryHandle;

        Steinberg::IPluginFactory* pluginFactory;

        std::string vendor;
        std::string url;
        std::string email;
        int32_t flags;

        void showWindow();

        Steinberg::FUnknownPtr<Steinberg::Vst::IComponent> component;

        Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> audioProcessor;
        Steinberg::FUnknownPtr<Steinberg::Vst::IEditController> editController;
        Steinberg::FUnknownPtr<Steinberg::Vst::IParameterChanges> parameterChanges;
        Steinberg::FUnknownPtr<Steinberg::Vst::IEventList> eventList;
        /*
        Steinberg::FUnknownPtr<Steinberg::Vst::class> className;
        */

        private:
        void loadPluginLibrary();
        void fetchPluginFactoryInfo();
        bool getID();
        bool instantiatePlugin();
        bool createEditControllerAndPlugView();


};

#endif
