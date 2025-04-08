#include "Plugin.h"

Plugin::Plugin() {
    std::string error;
    std::string path = "/usr/lib/vst3/Vital.vst3";
    
    VST3::Hosting::Module::Ptr module = VST3::Hosting::Module::create(path, error);

    Steinberg::IPtr<Steinberg::Vst::PlugProvider> plugProvider; 
    VST3::Optional<VST3::UID> effectUID = VST3::UID(); 

    VST3::Optional<VST3::UID> effectID = std::move(effectUID);
    for (auto& classInfo : module->
        getFactory().classInfos())
    {
        if (classInfo.category() == kVstAudioEffectClass)
        {
            if (effectID)
            {
                if (*effectID != classInfo.ID())
                    continue;
            }
                plugProvider = Steinberg::owned(new Steinberg::Vst::PlugProvider(module->getFactory(), classInfo, true));
                break;
        }
    }
    

}

