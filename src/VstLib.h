#pragma once
#include "PlugLib.h"

#include <memory>
#include <cstring> // for memcpy

#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"



class VstLib : public PlugLib {
public:
    VstLib(std::string);
    ~VstLib();

    Steinberg::IPluginFactory* getFactory() const { return factory; }
    const Steinberg::TUID& getComponentCID() const { return componentCID; }
    const Steinberg::TUID& getControllerCID() const { return controllerCID; }

    std::unique_ptr<VST3::Hosting::PluginFactory>& getFactoryWrapper() { return factoryWrapper; }

    std::unique_ptr<Steinberg::Vst::PlugProvider> plugProvider;

    bool getProvider();
    bool load() override;

private:
    Steinberg::FUnknownPtr<Steinberg::IPluginFactory> factory;
    std::unique_ptr<VST3::Hosting::PluginFactory> factoryWrapper;

    Steinberg::TUID componentCID{};
    Steinberg::TUID controllerCID{};

    bool fetchClassIDs();
};
