
#ifndef PLUGIN_H
#define PLUGIN_H


#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include "public.sdk/source/vst/hosting/plugprovider.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <string>

#include "EditorHostFrame.h"

class EditorHostFrame;


class Plugin {
    public:
        Plugin(const char* filepath);
        ~Plugin();

        EditorHostFrame* hostFrame;
        Steinberg::Vst::IComponentHandler* componentHandler;

         bool instantiate();
         
         void process(
            float* thrubuffer,
            int bufferSize
        );

        Steinberg::TUID componentCID;
        Steinberg::TUID controllerCID;

        Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> componentConnection;
        Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> controllerConnection;


        std::unique_ptr<Steinberg::Vst::PlugProvider> plugProvider;


        const char* filepath;

        void* pluginLibraryHandle;

        Steinberg::FUnknownPtr<Steinberg::IPluginFactory> pluginFactory;
        std::unique_ptr<VST3::Hosting::PluginFactory> factoryWrapper;


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
        bool createEditControllerAndPlugView(const Steinberg::TUID
        controllerCID);

        void makeConnection();


};

#endif
