#include "Instrument.h"
#include "Project.h"

Instrument::Instrument(Project* project) {
    edit = project->edit;
    engine = &project->engine;
    outputs.push_back(&project->tracks[0]);

        DBG("9");
    tracktion_engine::AudioTrack* track = outputs[0]->track;

    auto& knownPluginList = engine->getPluginManager().knownPluginList;
    juce::File pluginFile("/usr/lib/vst3/Vital.vst3/Contents/x86_64-linux/Vital.so");
    DBG("14");
    static juce::OwnedArray<juce::PluginDescription> pluginDescriptions;
    static juce::VST3PluginFormat vst3PluginFormat;
    DBG("17");
    DBG("Checking file: " + pluginFile.getFullPathName());
    if (pluginFile.existsAsFile()) {
        DBG("Plugin file exists: " + pluginFile.getFullPathName());
        knownPluginList.scanAndAddFile("/usr/lib/vst3/Vital.vst3", true, pluginDescriptions, vst3PluginFormat);
        DBG("Plugin format: " + vst3PluginFormat.getName());

        DBG("Plugin descriptions count: " + juce::String(pluginDescriptions.size()));
    
        if (pluginDescriptions.isEmpty())
        {
            DBG("No plugin descriptions found!");
        }
        else
        {
            for (int i = 0; i < pluginDescriptions.size(); ++i)
            {
                DBG("Plugin description: " + pluginDescriptions[i]->name);
            }
        }

    } else {
        DBG("Plugin file not found: " + pluginFile.getFullPathName());
    }
    
    DBG("26");

    if (pluginDescriptions.isEmpty()) {
        DBG("No plugin descriptions available.");
    }
    
    auto* pluginDescription = pluginDescriptions.getLast();
    if (pluginDescription == nullptr) {
        DBG("Last plugin description is null.");
    }

    plugin = edit->getPluginCache().createNewPlugin(tracktion_engine::ExternalPlugin::xmlTypeName, *pluginDescription);
    DBG("38");
       //add it to the track
       track->pluginList.insertPlugin(plugin, track->pluginList.size() + 1, nullptr);
       DBG("43");


   
       if (plugin != nullptr)
       DBG("Plugin pointer is valid");
   else
       DBG("Plugin is null");
   

   

       plugin->initialiseFully();

       // Check if the plugin has a valid windowState
       if (plugin->windowState != nullptr) {
           DBG("Plugin window state is valid, proceeding to show window.");
           juce::MessageManager::callAsync([this]() {
               //plugin->showWindowExplicitly();
           });
       } else {
           DBG("Plugin window state is invalid.");
       }
       

}
Instrument::~Instrument() {

}


