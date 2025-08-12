
#ifndef PLUGIN_H
#define PLUGIN_H


#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include "public.sdk/source/vst/hosting/plugprovider.h"

#include "PlugLib.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <string>

#include "EditorHostFrame.h"
#include "EditorWindowHost.h"

#include "PluginManager.h"
#include "AudioManager.h"

#include "EventList.h"

#include "Project.h"
#include "Processing.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class EditorHostFrame;

class Project;
class Rack;

class Plugin {
    public:
        Plugin(char* filepath);
        ~Plugin();

        std::unique_ptr<EditorHostFrame> hostFrame;
        std::unique_ptr<EditorWindowHost> editorHost;
        Steinberg::Vst::IComponentHandler* componentHandler;

         bool instantiate();

         AudioManager* am;
         
         void process(
            audioData,
            EventList*
        );

         void setup();

         Project* project;

         bool processing = false;

         void toggle();

        std::vector<Steinberg::Vst::AudioBusBuffers> inputBuses, outputBuses;
        std::vector<float*> outputBufferData;
        std::vector<float*> inputBufferData;
        Steinberg::Vst::ProcessData data{};

        PlugLib* lib;

        Steinberg::TUID componentCID;
        Steinberg::TUID controllerCID;

        Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> componentConnection;
        Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint> controllerConnection;


        std::unique_ptr<Steinberg::Vst::PlugProvider> plugProvider;

        const char* filepath;

        const char* name;

        Steinberg::FUnknownPtr<Steinberg::IPluginFactory> pluginFactory;
        std::unique_ptr<VST3::Hosting::PluginFactory> factoryWrapper;


        std::string vendor;
        std::string url;
        std::string email;
        int32_t flags;

        void showWindow();

        Steinberg::IPlugView* view;
        Steinberg::ViewRect viewRect;

        Steinberg::FUnknownPtr<Steinberg::Vst::IComponent> component;

        Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor> audioProcessor;
        Steinberg::FUnknownPtr<Steinberg::Vst::IEditController> editController;
        Steinberg::FUnknownPtr<Steinberg::Vst::IParameterChanges> parameterChanges;

        bool windowOpen = false;

        bool editorTick();
        Rack* rack;
        char* path;

        json toJSON();
        void fromJSON(json);

        std::vector<uint8_t> getState();
        void setState(std::vector<uint8_t>&);
        uint16_t id;
    private:

        void fetchPluginFactoryInfo();
        bool instantiatePlugin();
        bool createEditControllerAndPlugView(const Steinberg::TUID
        controllerCID);


        void makeConnection();

};

#endif
